#include "glt/core/backups.hpp"

#include "glt/core/log.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/strings.hpp"

#include <algorithm>

namespace glt::backups {
namespace {

std::vector<BackupEntry> collect(const std::wstring& dir, const wchar_t* pattern,
                                 const char* kind) {
    std::vector<BackupEntry> out;
    if (dir.empty()) return out;

    for (const auto& name : str::glob(dir, pattern)) {
        const std::wstring full = str::path_join(dir, name);
        if (!str::file_exists(full)) continue;

        BackupEntry e;
        e.path = full;
        e.filename = name;
        e.bytes = str::file_size(full);
        e.kind = kind;
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace

std::vector<BackupEntry> list_backups(const std::wstring& db_path) {
    std::vector<BackupEntry> out;
    if (db_path.empty()) return out;

    // Configs.db backups land beside the database itself.
    const std::wstring db_dir = str::path_parent(db_path);
    auto db_entries = collect(db_dir, L"Configs.db.bak-*", "db");
    out.insert(out.end(), db_entries.begin(), db_entries.end());

    // smk.conf backups land beside smk.conf, which is a DIFFERENT tree
    // (%APPDATA%\Tencent\GameLoop\config\<pkg>\). The Python build globbed for
    // them in the database directory, where they are never written — so the
    // Backups page showed db backups only and the smk restore path was
    // unreachable from the UI. Both directories are scanned here.
    const std::wstring smk_dir = str::path_parent(paths::smk_conf());
    if (!smk_dir.empty() && str::iequals(smk_dir, db_dir)) {
        auto smk_entries = collect(smk_dir, L"smk.conf.bak-*", "smk");
        out.insert(out.end(), smk_entries.begin(), smk_entries.end());
    }

    // The timestamp suffix sorts chronologically within a kind, so the plain
    // path sort gives the Backups page "newest last" for free.
    std::sort(out.begin(), out.end(), [](const BackupEntry& a, const BackupEntry& b) {
        return a.path < b.path;
    });
    return out;
}

std::vector<std::string> restore_db(const std::wstring& db_path, const std::wstring& bak_path) {
    std::vector<std::string> notes;

    if (!str::file_exists(bak_path)) {
        notes.push_back("restore failed: backup not found");
        return notes;
    }
    if (db_path.empty()) {
        notes.push_back("restore failed: Configs.db path unknown");
        return notes;
    }

    // The destination may be carrying a ReadOnly attribute from a previous
    // restore; clear it so the copy can land.
    str::set_readonly(db_path, false);

    if (!str::copy_file(bak_path, db_path)) {
        notes.push_back("restore failed: could not copy " + str::to_utf8(bak_path));
        log::error(notes.back());
        return notes;
    }
    notes.push_back("restored " + str::to_utf8(str::path_filename(bak_path)));

    // Drop the sidecars so SQLite rebuilds them from the restored file. Copying
    // the backup's -wal would replay transactions the backup never contained.
    for (const auto* ext : {L"-wal", L"-shm"}) {
        const std::wstring sidecar = db_path + ext;
        if (str::remove_file(sidecar)) {
            notes.push_back(std::string("dropped ") + str::to_utf8(ext) +
                            " (rebuilds on launch)");
        }
    }

    log::info("restored Configs.db from " + str::to_utf8(bak_path));
    return notes;
}

std::vector<std::string> restore_smk(const std::wstring& smk_path, const std::wstring& bak_path) {
    std::vector<std::string> notes;

    if (!str::file_exists(bak_path)) {
        notes.push_back("restore failed: backup not found");
        return notes;
    }
    if (smk_path.empty()) {
        notes.push_back("restore failed: smk.conf path unknown");
        return notes;
    }

    // smk.conf is deliberately locked read-only by the quarantine fix. It MUST
    // be unlocked before the restore, or this silently does nothing.
    str::set_readonly(smk_path, false);

    if (!str::copy_file(bak_path, smk_path)) {
        notes.push_back("restore failed: could not copy " + str::to_utf8(bak_path));
        return notes;
    }
    notes.push_back("restored " + str::to_utf8(str::path_filename(bak_path)) +
                    " (templates back, read-only lock cleared)");
    return notes;
}

} // namespace glt::backups