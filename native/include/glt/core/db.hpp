// ─────────────────────────────────────────────────────────────────────────────
// glt/core/db.hpp — Configs.db access: online backup, guarded writes.
//
// Ported from legacy-python/src/core/db.py.
//
// THE ONE THING THAT MUST NOT BE DONE NAIVELY
// --------------------------------------------
// GameLoop holds Configs.db open in WAL mode. On this rig the -wal sidecar was
// 1.3 MB against a 557 KB main file, i.e. most of the committed data had not
// been checkpointed into the main file at snapshot time. Copying Configs.db
// (+/- the -wal/-shm files) while GameLoop runs produces a TORN backup that
// sqlite3 will happily reject on open.
//
// db.py therefore used Python's Connection.backup(), which is a wrapper over
// sqlite3_backup_init/step/finish and yields the source lock between steps.
// That is reproduced here with the raw C API. CopyFileW is kept ONLY as a
// last-resort fallback for the case where the source DB is corrupt enough
// that the backup API refuses to start.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace glt::db {

// ── Errors ───────────────────────────────────────────────────────────────────

class Error : public std::runtime_error {
public:
    explicit Error(const std::string& what) : std::runtime_error(what) {}
};

// ── Values ───────────────────────────────────────────────────────────────────

// Mirrors the small set of SQLite storage classes the keymap tables actually
// use. Everything GameLoop stores is TEXT, but REAL shows up in fps/cadence
// fields on some builds and a silent "store as text" would change comparisons.
class Value {
public:
    enum class Kind { Null, Int, Real, Text, Blob };

    Value() = default;
    Value(std::nullptr_t) {}
    Value(int v) : kind_(Kind::Int), i_(v) {}
    Value(long long v) : kind_(Kind::Int), i_(v) {}
    Value(double v) : kind_(Kind::Real), d_(v) {}
    Value(std::string v) : kind_(Kind::Text), s_(std::move(v)) {}
    Value(std::string_view v) : kind_(Kind::Text), s_(v) {}
    Value(const char* v) : kind_(Kind::Text), s_(v ? v : "") {}
    Value(std::wstring_view v);
    Value(bool v) : kind_(Kind::Int), i_(v ? 1 : 0) {}

    [[nodiscard]] Kind kind() const { return kind_; }
    [[nodiscard]] bool is_null() const { return kind_ == Kind::Null; }

    [[nodiscard]] std::int64_t as_int() const;
    [[nodiscard]] double as_real() const;
    [[nodiscard]] std::string as_text() const;

    // Convenience casts used all over the callers, e.g. registry QWORDs that
    // SQLite handed back as text on some GameLoop builds.
    [[nodiscard]] std::int64_t as_int_or(std::int64_t fallback) const;
    [[nodiscard]] bool as_bool_or(bool fallback) const;
    [[nodiscard]] std::string as_text_or(std::string_view fallback) const;

    // Process-wide null singleton, used as the out-of-range row value.
    [[nodiscard]] static const Value& as_null_ref();

    // Bind helper.
    void bind(sqlite3_stmt* stmt, int index) const;

private:
    Kind kind_ = Kind::Null;
    std::int64_t i_ = 0;
    double d_ = 0.0;
    std::string s_;
};

// ── Row ──────────────────────────────────────────────────────────────────────

struct Row {
    std::vector<Value> cols;

    // Out-of-range access returns a null Value rather than crashing: every
    // caller here does `row.value0()` on a query whose column list it controls,
    // but a future schema change should degrade, not fault.
    [[nodiscard]] const Value& at(std::size_t i) const;
};

// ── Connection ───────────────────────────────────────────────────────────────

// Move-only RAII wrapper around sqlite3*. Opening Configs.db fails routinely
// (GameLoop mid-update, file locked) so the constructor throws Error and every
// caller is expected to handle it — mirroring Python's sqlite3.OperationalError.
class Connection {
public:
    // timeout_ms maps to sqlite3_busy_timeout. Python used 15 s for writes and
    // 5 s for read-only probes; that distinction is preserved by the callers.
    explicit Connection(const std::wstring& path, int timeout_ms = 15000);
    ~Connection();

    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    [[nodiscard]] sqlite3* handle() const { return db_; }
    [[nodiscard]] const std::wstring& path() const { return path_; }
    [[nodiscard]] bool valid() const { return db_ != nullptr; }

    // Query with bound parameters. Returns every row; these tables top out in
    // the low hundreds of rows so materialising is cheaper than streaming.
    [[nodiscard]] std::vector<Row> query(std::string_view sql,
                                        const std::vector<Value>& params = {}) const;

    // First column of the first row, or nullopt when there are no rows.
    [[nodiscard]] std::optional<Value> query_one(std::string_view sql,
                                                 const std::vector<Value>& params = {}) const;

    // Runs a statement with no result set. Returns sqlite3_changes().
    [[nodiscard]] int execute(std::string_view sql, const std::vector<Value>& params = {});

    // BEGIN IMMEDIATE / COMMIT / ROLLBACK.
    void begin();
    void commit();
    void rollback();

    // PRAGMA wal_checkpoint(TRUNCATE) — collapses the -wal into the main file.
    // Called after every guarded write so the next backup sees a quiescent DB.
    void checkpoint_wal();

private:
    sqlite3* db_ = nullptr;
    std::wstring path_;
};

// ── Backup ───────────────────────────────────────────────────────────────────

// Consistent snapshot via the SQLite online backup API, written to
//   <db dir>\Configs.db.bak-YYYYMMDD-HHMMSS
// Returns the destination path, or "" on total failure.
// A second-resolution timestamp means two backups inside the same second would
// collide; the suffix gets a -2, -3 … disambiguator in that case.
[[nodiscard]] std::wstring backup(const std::wstring& db_path);

// ── Guarded updates ──────────────────────────────────────────────────────────

// One deferred write: SQL + bound params + a human description for the log.
struct Op {
    std::string sql;
    std::vector<Value> params;
    std::string describe;
};

// Returns per-op notes for the UI console. Throws Error after rolling back if
// any statement fails — the caller surfaces it as a failed fix.
//
// dry_run short-circuits before taking a backup, matching db.py: it still
// reports how many ops *would* run so the UI shows a non-empty result.
struct GuardedResult {
    std::vector<std::string> notes;
};

GuardedResult guarded_updates(const std::wstring& db_path, const std::vector<Op>& ops,
                              bool dry_run = false,
                              const std::function<std::vector<std::string>(const Connection&)>&
                                  verify = {});

// ── Introspection ────────────────────────────────────────────────────────────

struct TriggerInfo {
    std::string name;
    std::string table;
    std::string sql;
};

// The _cache_upd / _cache_ins / _cache_del triggers GameLoop installs. These
// hard-FAIL any write to offcial_config2.<pkg> and the two offcial_keymap
// rows, which is why the fixes only ever touch keymap_data.unlogin.* blobs.
[[nodiscard]] std::vector<TriggerInfo> triggers(const Connection& con);

// Keys listed in a trigger's WHEN clause. Probing the table is cheaper and more
// robust than parsing trigger SQL, which is what db.py did too.
[[nodiscard]] std::vector<std::string> guarded_keys(const Connection& con);

} // namespace glt::db