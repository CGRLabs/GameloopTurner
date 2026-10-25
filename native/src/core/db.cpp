#include "glt/core/db.hpp"

#include "glt/core/log.hpp"
#include "glt/core/strings.hpp"

#include <sqlite3.h>

#include <windows.h> // Sleep

#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>

namespace glt::db {
namespace {

// SQLite's per-connection fatal state. Every public entry point checks it so a
// failed prepare/step surfaces as a C++ exception rather than a silently
// short statement list.
void check(sqlite3* db, int rc, std::string_view what) {
    if (rc == SQLITE_OK || rc == SQLITE_ROW || rc == SQLITE_DONE) return;
    const char* msg = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    throw Error(std::string(what) + ": " + (msg ? msg : sqlite3_errstr(rc)));
}

// Finaliser for the backup destination connection.
struct BackupDestDeleter {
    void operator()(sqlite3* p) const noexcept {
        if (p) sqlite3_close(p);
    }
};
using BackupDest = std::unique_ptr<sqlite3, BackupDestDeleter>;

// How many times to retry a backup page step. The source may be locked by
// GameLoop mid-write; the API asks us to sleep and retry, but we cap it so a
// pathologically busy DB cannot hang the apply worker forever.
constexpr int kBackupMaxRetries = 40;   // ~4 s at the 100 ms backoff below
constexpr int kBackupSleepMs = 100;

std::wstring unique_backup_path(const std::wstring& db_path) {
    const std::wstring dir = str::path_parent(db_path);
    const std::wstring stamp = str::timestamp_compact_w();
    const std::wstring base = L"Configs.db.bak-" + stamp;

    std::wstring candidate = str::path_join(dir, base);
    int n = 2;
    while (str::file_exists(candidate)) {
        candidate = str::path_join(dir, base + L"-" + std::to_wstring(n));
        ++n;
    }
    return candidate;
}

} // namespace

// ── Value ────────────────────────────────────────────────────────────────────

Value::Value(std::wstring_view v)
    : kind_(Kind::Text), s_(str::to_utf8(v)) {}

std::int64_t Value::as_int() const {
    switch (kind_) {
        case Kind::Int:  return i_;
        case Kind::Real: return static_cast<std::int64_t>(d_);
        case Kind::Text: {
            // Registry QWORDS are stored by GameLoop as TEXT in several places;
            // a tolerant parse keeps every caller from writing its own.
            try {
                return std::stoll(s_);
            } catch (...) {
                return 0;
            }
        }
        default: return 0;
    }
}

double Value::as_real() const {
    switch (kind_) {
        case Kind::Int:  return static_cast<double>(i_);
        case Kind::Real: return d_;
        case Kind::Text: {
            try {
                return std::stod(s_);
            } catch (...) {
                return 0.0;
            }
        }
        default: return 0.0;
    }
}

std::string Value::as_text() const {
    switch (kind_) {
        case Kind::Text: return s_;
        case Kind::Int:  return std::to_string(i_);
        case Kind::Real: {
            std::string out(32, '\0');
            const int n = std::snprintf(out.data(), out.size(), "%g", d_);
            if (n > 0) out.resize(static_cast<std::size_t>(n));
            return out;
        }
        default: return {};
    }
}

std::int64_t Value::as_int_or(std::int64_t fallback) const {
    return is_null() ? fallback : as_int();
}

bool Value::as_bool_or(bool fallback) const {
    if (is_null()) return fallback;
    if (kind_ == Kind::Text) {
        const auto t = str::trim(s_);
        if (t.empty()) return false;
        if (str::iequals(t, "false") || t == "0") return false;
        return true;
    }
    return as_int() != 0;
}

std::string Value::as_text_or(std::string_view fallback) const {
    return is_null() ? std::string(fallback) : as_text();
}

void Value::bind(sqlite3_stmt* stmt, int index) const {
    switch (kind_) {
        case Kind::Null: sqlite3_bind_null(stmt, index); break;
        case Kind::Int:  sqlite3_bind_int64(stmt, index, i_); break;
        case Kind::Real: sqlite3_bind_double(stmt, index, d_); break;
        case Kind::Text:
            sqlite3_bind_text(stmt, index, s_.c_str(), static_cast<int>(s_.size()),
                              SQLITE_TRANSIENT);
            break;
        case Kind::Blob:
            sqlite3_bind_blob(stmt, index, s_.data(), static_cast<int>(s_.size()),
                              SQLITE_TRANSIENT);
            break;
    }
}

// ── Row ──────────────────────────────────────────────────────────────────────

// Process-wide null, returned for out-of-range column indices.
const Value& Value::as_null_ref() {
    static const Value kNull;
    return kNull;
}

const Value& Row::at(std::size_t i) const {
    return i < cols.size() ? cols[i] : Value::as_null_ref();
}

// ── Connection ───────────────────────────────────────────────────────────────

Connection::Connection(const std::wstring& path, int timeout_ms) : path_(path) {
    if (path.empty()) throw Error("Configs.db path is empty");

    // sqlite3_open_v2 takes UTF-8, sqlite3_open16 takes UTF-16. UTF-8 is used
    // because the amalgamation is compiled without SQLITE_OMIT_UTF16 and
    // because a non-ASCII install path (a user profile or a GameLoop folder
    // with an accented or CJK name) round-trips correctly through it.
    const std::string utf8_path = str::to_utf8(path);

    // SQLITE_OPEN_READWRITE | CREATE: GameLoop's own schema changes between
    // builds and we must never be the thing that refuses to open a newer file.
    const int rc = sqlite3_open_v2(
        utf8_path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);

    if (rc != SQLITE_OK) {
        // sqlite3_open_v2 hands back a handle even on failure so the error
        // string can be read off it; close it before bailing out.
        const std::string msg = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw Error("cannot open " + str::to_utf8(path) + ": " + msg);
    }

    // FULLMUTEX was requested so a Connection is safe to share across the
    // drift thread and the apply worker; the busy timeout then handles the
    // remaining contention.
    sqlite3_busy_timeout(db_, timeout_ms);
}

Connection::~Connection() {
    if (db_) sqlite3_close(db_);
}

Connection::Connection(Connection&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), path_(std::move(other.path_)) {}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        if (db_) sqlite3_close(db_);
        db_ = std::exchange(other.db_, nullptr);
        path_ = std::move(other.path_);
    }
    return *this;
}

std::vector<Row> Connection::query(std::string_view sql,
                                   const std::vector<Value>& params) const {
    std::vector<Row> out;

    sqlite3_stmt* stmt = nullptr;
    check(db_, sqlite3_prepare_v2(db_, std::string(sql).c_str(), -1, &stmt, nullptr),
          "prepare");

    for (std::size_t i = 0; i < params.size(); ++i) {
        params[i].bind(stmt, static_cast<int>(i + 1));
    }

    while (true) {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            const std::string msg = sqlite3_errmsg(db_);
            sqlite3_finalize(stmt);
            throw Error(std::string(sql) + ": " + msg);
        }

        Row row;
        const int cols = sqlite3_column_count(stmt);
        row.cols.reserve(static_cast<std::size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            switch (sqlite3_column_type(stmt, c)) {
                case SQLITE_INTEGER:
                    row.cols.emplace_back(
                        static_cast<long long>(sqlite3_column_int64(stmt, c)));
                    break;
                case SQLITE_FLOAT:
                    row.cols.emplace_back(sqlite3_column_double(stmt, c));
                    break;
                case SQLITE_TEXT: {
                    const auto* p = sqlite3_column_text(stmt, c);
                    const int n = sqlite3_column_bytes(stmt, c);
                    row.cols.emplace_back(std::string(reinterpret_cast<const char*>(p),
                                                      static_cast<std::size_t>(n)));
                    break;
                }
                case SQLITE_BLOB: {
                    const auto* p = sqlite3_column_blob(stmt, c);
                    const int n = sqlite3_column_bytes(stmt, c);
                    row.cols.emplace_back(std::string(static_cast<const char*>(p),
                                                      static_cast<std::size_t>(n)));
                    break;
                }
                default:
                    row.cols.emplace_back(nullptr);
                    break;
            }
        }
        out.push_back(std::move(row));
    }

    sqlite3_finalize(stmt);
    return out;
}

std::optional<Value> Connection::query_one(std::string_view sql,
                                           const std::vector<Value>& params) const {
    auto rows = query(sql, params);
    if (rows.empty() || rows[0].cols.empty()) return std::nullopt;
    return rows[0].cols[0];
}

int Connection::execute(std::string_view sql, const std::vector<Value>& params) {
    sqlite3_stmt* stmt = nullptr;
    check(db_, sqlite3_prepare_v2(db_, std::string(sql).c_str(), -1, &stmt, nullptr),
          "prepare");

    for (std::size_t i = 0; i < params.size(); ++i) {
        params[i].bind(stmt, static_cast<int>(i + 1));
    }

    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        const std::string msg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        throw Error(std::string(sql) + ": " + msg);
    }
    sqlite3_finalize(stmt);
    return sqlite3_changes(db_);
}

void Connection::begin() { execute("BEGIN IMMEDIATE"); }

void Connection::commit() { execute("COMMIT"); }

void Connection::rollback() {
    // Rollback must never throw: it is called from the failure path of
    // guarded_updates(), where an exception would replace the real error.
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
}

void Connection::checkpoint_wal() {
    // TRUNCATE folds the -wal back into the main file and resets the sidecar
    // to zero bytes, which is what lets the next online backup be fast and
    // keeps the user's AppData folder from growing without bound.
    sqlite3_wal_checkpoint_v2(db_, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
}

// ── Backup ───────────────────────────────────────────────────────────────────

std::wstring backup(const std::wstring& db_path) {
    if (db_path.empty() || !str::file_exists(db_path)) {
        log::error("backup: source database not found");
        return {};
    }

    const std::wstring dst = unique_backup_path(db_path);

    // ---- Preferred path: SQLite online backup API ----------------------------
    // Consistent snapshot even while GameLoop holds the DB open, because the
    // API restarts the copy whenever the source page changes underneath us.
    // Paths go through UTF-8, matching Connection above.
    const std::string src_utf8 = str::to_utf8(db_path);
    const std::string dst_utf8 = str::to_utf8(dst);
    try {
        sqlite3* src_raw = nullptr;
        int rc = sqlite3_open_v2(src_utf8.c_str(), &src_raw, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK) {
            if (src_raw) sqlite3_close(src_raw);
            throw Error("source open failed");
        }

        // RAII both ends so an exception mid-copy cannot leak a handle or leave
        // the destination locked. `BackupDest` is the unique_ptr ALIAS — passing
        // it as a template argument here would produce
        // unique_ptr<sqlite3, unique_ptr<sqlite3, ...>>, which is the kind of
        // mistake the compiler reports several hundred lines away from its
        // cause.
        BackupDest src(src_raw);

        sqlite3* dst_raw = nullptr;
        rc = sqlite3_open_v2(dst_utf8.c_str(), &dst_raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                            nullptr);
        if (rc != SQLITE_OK) {
            if (dst_raw) sqlite3_close(dst_raw);
            throw Error("destination open failed");
        }
        BackupDest dest(dst_raw);

        sqlite3_backup* b = sqlite3_backup_init(dest.get(), "main", src.get(), "main");
        if (!b) throw Error(std::string("backup_init: ") + sqlite3_errmsg(dest.get()));

        int tries = 0;
        int rc_step = SQLITE_OK;
        do {
            rc_step = sqlite3_backup_step(b, 256);
            if (rc_step == SQLITE_OK || rc_step == SQLITE_DONE) continue;

            if (rc_step == SQLITE_BUSY || rc_step == SQLITE_LOCKED) {
                if (++tries > kBackupMaxRetries) {
                    sqlite3_backup_finish(b);
                    throw Error("source stayed locked through the whole backup");
                }
                ::Sleep(kBackupSleepMs);
            } else {
                const std::string msg = sqlite3_errmsg(dest.get());
                sqlite3_backup_finish(b);
                throw Error("backup_step: " + msg);
            }
        } while (rc_step != SQLITE_DONE);

        sqlite3_backup_finish(b);

        // Match the Python build: the snapshot is a single self-contained file
        // with no -wal/-shm sidecars, because the backup API folds the WAL into
        // the destination as it goes.
        log::info("backup " + str::to_utf8(dst));
        return dst;
    } catch (const Error& e) {
        log::warn(std::string("online backup failed (") + e.what() +
                  ") — falling back to a cold file copy");
    }

    // ---- Fallback: raw copy --------------------------------------------------
    // Only reached when the source is corrupt enough that the backup API
    // refuses to start. A cold copy of a live WAL DB is torn, but it is still
    // strictly better than having no backup at all before a destructive write.
    std::string bytes;
    if (!str::read_file(db_path, bytes) || !str::write_file(dst, bytes)) {
        log::error("backup failed entirely");
        str::remove_file(dst);
        return {};
    }
    log::warn("cold-copy backup " + str::to_utf8(dst));
    return dst;
}

// ── Guarded updates ──────────────────────────────────────────────────────────

GuardedResult guarded_updates(
    const std::wstring& db_path, const std::vector<Op>& ops, bool dry_run,
    const std::function<std::vector<std::string>(const Connection&)>& verify) {

    GuardedResult result;

    if (dry_run) {
        result.notes.push_back("DRYRUN: " + std::to_string(ops.size()) + " ops skipped");
        return result;
    }

    // 1. Snapshot before touching anything.
    const std::wstring dst = backup(db_path);
    if (dst.empty()) {
        throw Error("refusing to write: no backup could be taken");
    }
    result.notes.push_back("backup: " + str::to_utf8(dst));

    // 2. Apply inside one transaction.
    Connection con(db_path);
    try {
        con.begin();
        for (const auto& op : ops) {
            const int rows = con.execute(op.sql, op.params);
            result.notes.push_back(op.describe + " (rows=" + std::to_string(rows) + ")");
        }
        con.commit();
    } catch (const Error&) {
        // Roll back, then re-throw. The backup taken in step 1 remains on disk
        // so the user can restore manually if they want to.
        con.rollback();
        throw Error("write failed, rolled back (backup kept at " + str::to_utf8(dst) + ")");
    }

    // 3. Collapse the WAL so the next backup is fast and the sidecar shrinks.
    con.checkpoint_wal();

    // 4. Re-read to prove the write actually stuck. This is what distinguishes
    //    "we never wrote it" from "the engine wrote it back a second later".
    if (verify) {
        try {
            auto notes = verify(con);
            result.notes.insert(result.notes.end(), notes.begin(), notes.end());
        } catch (const std::exception& e) {
            // The write is already committed; a verify failure is diagnostic
            // information, not an operation failure.
            result.notes.push_back(std::string("verify failed: ") + e.what());
        }
    }

    return result;
}

// ── Introspection ────────────────────────────────────────────────────────────

std::vector<TriggerInfo> triggers(const Connection& con) {
    std::vector<TriggerInfo> out;
    const auto rows = con.query(
        "SELECT name, tbl_name, sql FROM sqlite_master WHERE type='trigger'");
    for (const auto& r : rows) {
        out.push_back({r.at(0).as_text(), r.at(1).as_text(), r.at(2).as_text()});
    }
    return out;
}

std::vector<std::string> guarded_keys(const Connection& con) {
    std::vector<std::string> out;
    // NOTE: 'offcial' is GameLoop's own typo for 'official' — preserved
    // verbatim because it is what the trigger's WHEN clause matches on.
    const auto rows = con.query(
        "SELECT key FROM key_map_configs_cache_official WHERE key LIKE 'offcial%'");
    for (const auto& r : rows) out.push_back(r.at(0).as_text());
    return out;
}

} // namespace glt::db