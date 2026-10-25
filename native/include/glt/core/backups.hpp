// ─────────────────────────────────────────────────────────────────────────────
// glt/core/backups.hpp — enumerate and restore the timestamped backups.
//
// Ported from legacy-python/src/core/backups.py.
//
// Backup naming (the Backups page and Restore From Backup depend on it):
//   <db dir>\Configs.db.bak-YYYYMMDD-HHMMSS
//   <smk.conf dir>\smk.conf.bak-YYYYMMDD-HHMMSS
//
// Restoring drops the -wal and -shm sidecars rather than restoring them. They
// belong to the backup's transaction history, and replaying them against a
// restored main file can resurrect writes the backup deliberately excluded.
// SQLite rebuilds both on next launch.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace glt::backups {

struct BackupEntry {
    std::wstring path;
    std::uint64_t bytes = 0;
    std::wstring filename;
    // "db" or "smk" — which restore path this entry feeds.
    std::string kind;
};

// Every backup next to `db_path` (Configs.db.*) plus every smk.conf backup.
// Sorted by path, which for this timestamp format is also chronological order.
[[nodiscard]] std::vector<BackupEntry> list_backups(const std::wstring& db_path);

// Restores `bak_path` over `db_path`. Caller must have closed GameLoop first:
// copying the main file while the emulator holds a 1.3 MB WAL open produces a
// database that is neither the backup nor the current state.
[[nodiscard]] std::vector<std::string> restore_db(const std::wstring& db_path,
                                                  const std::wstring& bak_path);

// Restores a quarantined smk.conf (the unquarantine path).
[[nodiscard]] std::vector<std::string> restore_smk(const std::wstring& smk_path,
                                                  const std::wstring& bak_path);

} // namespace glt::backups