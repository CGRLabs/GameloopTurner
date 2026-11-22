// glt/core/preflight.cpp — see glt/core/preflight.hpp for the design notes.
#include "glt/core/preflight.hpp"

#include "glt/core/keymaps.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/strings.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <windows.h>

namespace glt::preflight {
namespace {

Check make(const char* name, Status status, std::string detail = {}) {
    return Check{name, status, std::move(detail)};
}

void accumulate(Report& rep, Check c) {
    if (c.status == Status::Fail) {
        rep.overall = Status::Fail;
    } else if (c.status == Status::Warn && rep.overall != Status::Fail) {
        rep.overall = Status::Warn;
    }
    rep.checks.push_back(std::move(c));
}

// PRAGMA table_info returns (cid, name, type, notnull, dflt_value, pk).
std::set<std::string> table_columns(const db::Connection& con, const char* table) {
    std::set<std::string> names;
    for (const auto& row : con.query(std::string("PRAGMA table_info(") + table + ")")) {
        names.insert(row.at(1).as_text());
    }
    return names;
}

} // namespace

Report run_db_checks(const db::Connection& con) {
    Report rep;

    // ── Expected tables ──────────────────────────────────────────────────────
    // 'offcial' is GameLoop's own misspelling and must not be corrected.
    auto table_exists = [&con](const char* t) {
        return con.query_one(
                   "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
                   {db::Value(t)}) != std::nullopt;
    };
    if (table_exists("key_map_configs_cache_official")) {
        accumulate(rep, make("keymap table present", Status::Ok));
    } else {
        accumulate(rep, make("keymap table present", Status::Fail,
                             "expected table key_map_configs_cache_official is missing - the "
                             "GameLoop schema changed and the fixes cannot run safely"));
    }
    if (table_exists("configs")) {
        accumulate(rep, make("configs table present", Status::Ok));
    } else {
        accumulate(rep, make("configs table present", Status::Fail,
                             "expected table configs is missing - the GameLoop schema "
                             "changed and the fixes cannot run safely"));
    }

    // ── Expected columns ─────────────────────────────────────────────────────
    if (table_exists("key_map_configs_cache_official")) {
        const auto cols = table_columns(con, "key_map_configs_cache_official");
        const bool ok = cols.count("key") && cols.count("value0") && cols.count("value1");
        if (ok) {
            accumulate(rep, make("keymap table columns", Status::Ok));
        } else {
            accumulate(rep, make("keymap table columns", Status::Fail,
                                 "key_map_configs_cache_official no longer has the (key, "
                                 "value0, value1) layout - the GameLoop schema changed"));
        }
    }

    // ── Engine keymap state initialised ──────────────────────────────────────
    // GameLoop writes keymap_mode rows on first launch for the account. Zero
    // rows usually means "never launched", not "schema change" — a warning,
    // because the fixes discover modes at runtime and would simply find none.
    // The query itself must be exception-safe: on a schema-broken install the
    // `key` column may be gone entirely, and a preflight that crashes on the
    // very installs it exists to detect would defeat its own purpose.
    {
        std::optional<db::Value> n;
        bool readable = true;
        try {
            n = con.query_one(
                "SELECT COUNT(*) FROM key_map_configs_cache_official "
                "WHERE key LIKE 'keymap_mode.unlogin.%'");
        } catch (const db::Error&) {
            readable = false;
        }
        const long long rows = readable && n ? n->as_int_or(-1) : -1;
        if (rows > 0) {
            accumulate(rep, make("engine keymap state", Status::Ok));
        } else if (rows == 0) {
            accumulate(rep, make("engine keymap state", Status::Warn,
                                 "no keymap_mode.unlogin.* rows yet - launch GameLoop once "
                                 "for this account, then re-run the fixes"));
        } else {
            accumulate(rep, make("engine keymap state", Status::Warn,
                                 "the keymap row count could not be read"));
        }
    }

    return rep;
}

std::vector<Check> run_smk_checks(const std::wstring& smk_path) {
    std::vector<Check> checks;

    // The FILE is the operative source for the jump triggers, so its shape is
    // load-bearing. Missing/unreadable degrades the blind fix to DB-only (the
    // fix already reports that), and an empty template set is ambiguous: it is
    // both "already blinded" and "format changed". The read-only lock is the
    // tiebreaker: a locked file with no templates is exactly the healthy
    // post-quarantine state, so it is OK.
    if (smk_path.empty()) {
        checks.push_back(make("smk.conf present", Status::Warn,
                              "smk.conf not found - template blinding will be DB-only; "
                              "re-check after GameLoop fully launches for this account"));
        return checks;
    }
    checks.push_back(make("smk.conf present", Status::Ok));

    std::string text;
    if (str::read_file(smk_path, text)) {
        checks.push_back(make("smk.conf readable", Status::Ok));

        const auto tex = keymaps::tex_ids_in_text(text);
        if (!tex.empty()) {
            checks.push_back(make("smk.conf templates", Status::Ok));
        } else if (str::is_readonly(smk_path)) {
            checks.push_back(make("smk.conf templates", Status::Ok,
                                  "no Tex templates and the file is read-only: already blinded"));
        } else {
            checks.push_back(make("smk.conf templates", Status::Warn,
                                  "smk.conf contains no Tex templates and is not read-only - "
                                  "either already blinded (re-apply the recommended chain to "
                                  "be sure) or the template format changed"));
        }
    } else {
        checks.push_back(make("smk.conf readable", Status::Warn,
                              "smk.conf could not be read - template blinding may not be "
                              "able to verify its state"));
    }
    return checks;
}

Report run() {
    Report rep;

    // ── Discovery ────────────────────────────────────────────────────────────
    const std::wstring root = paths::install_root();
    if (root.empty()) {
        accumulate(rep, make("GameLoop install found", Status::Fail,
                             "install root not found on any fixed drive or in the registry"));
    } else {
        accumulate(rep, make("GameLoop install found", Status::Ok));
    }

    const std::wstring dbp = paths::appdata_db();
    if (dbp.empty()) {
        // Without the DB nothing else is answerable. This is the same state the
        // Dashboard reports as "is GameLoop installed and launched?", so the
        // wording matches it.
        accumulate(rep, make("Configs.db found", Status::Fail,
                             "Configs.db not found - is GameLoop installed and launched "
                             "for this user?"));
        return rep;
    }
    accumulate(rep, make("Configs.db found", Status::Ok));

    // ── The database opens ───────────────────────────────────────────────────
    std::optional<db::Connection> con;
    try {
        con.emplace(dbp, 5000);
        accumulate(rep, make("Configs.db opens", Status::Ok));
    } catch (const db::Error& e) {
        accumulate(rep, make("Configs.db opens", Status::Fail, e.what()));
        return rep;
    }

    for (auto& c : run_db_checks(*con).checks) accumulate(rep, std::move(c));
    for (auto& c : run_smk_checks(paths::smk_conf())) accumulate(rep, std::move(c));
    for (auto& c : run_version_checks()) accumulate(rep, std::move(c));

    return rep;
}

// ── GameLoop version checks ─────────────────────────────────────────────────────

namespace {

std::wstring read_reg_string(HKEY root, std::wstring_view subkey,
                             std::wstring_view value_name) {
    HKEY k = nullptr;
    if (::RegOpenKeyExW(root, subkey.data(), 0, KEY_READ, &k) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buf[256]{};
    DWORD cb = static_cast<DWORD>(sizeof(buf));
    const LONG rc = ::RegGetValueW(k, nullptr, value_name.data(),
                                   RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buf, &cb);
    ::RegCloseKey(k);
    if (rc != ERROR_SUCCESS) return {};
    return std::wstring(buf);
}

} // namespace

std::vector<Check> run_version_checks() {
    std::vector<Check> out;

    const std::wstring hkcu_ver =
        read_reg_string(HKEY_CURRENT_USER, L"Software\\Tencent\\GameLoop\\GameLoop", L"Version");
    const std::wstring hklm_ver =
        read_reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Tencent\\GameLoop", L"Version");

    // One report line per source; missing is informational, not fatal.
    if (!hkcu_ver.empty()) {
        out.push_back(make("GameLoop version (HKCU)", Status::Ok,
                           std::string("hkcu=") + str::to_utf8(hkcu_ver)));
    } else {
        out.push_back(make("GameLoop version (HKCU)", Status::Warn,
                           "GameLoop Version not found under HKCU - is GameLoop installed?"));
    }
    if (!hklm_ver.empty()) {
        out.push_back(make("GameLoop version (HKLM)", Status::Ok,
                           std::string("hklm=") + str::to_utf8(hklm_ver)));
    } else {
        out.push_back(make("GameLoop version (HKLM)", Status::Warn,
                           "GameLoop Version not found under HKLM"));
    }

    // Compare HKLM (the canonical installer version) against validated builds.
    // A mismatch does NOT fail preflight — the fixes are structural and may
    // still apply — but a Warn tells the user this build hasn't been validated.
    if (!hklm_ver.empty()) {
        const bool known = std::any_of(validated_gameloop_versions().begin(),
                                       validated_gameloop_versions().end(),
                                       [&](const std::string& v) {
                                           return str::iequals(v, str::to_utf8(hklm_ver));
                                       });
        if (!known) {
            out.push_back(make("GameLoop version validated", Status::Warn,
                               "running GameLoop " + str::to_utf8(hklm_ver) +
                                   " - keymap fix assumptions (schema, Tex ids, mode ids) "
                                   "were reverse-engineered against 7.0.167.0"));
        } else {
            out.push_back(make("GameLoop version validated", Status::Ok,
                               "running GameLoop " + str::to_utf8(hklm_ver) +
                                   " - validated build"));
        }
    }

    return out;
}

const std::vector<std::string>& validated_gameloop_versions() {
    static const std::vector<std::string> kVersions = {"7.0.167.0"};
    return kVersions;
}

std::string Report::summary_line() const {
    std::size_t warns = 0;
    std::size_t fails = 0;
    for (const auto& c : checks) {
        if (c.status == Status::Fail) ++fails;
        if (c.status == Status::Warn) ++warns;
    }
    if (fails > 0) {
        return std::to_string(fails) + " check(s) failed";
    }
    if (warns > 0) {
        return std::to_string(warns) + " warning(s)";
    }
    return "all checks passed";
}

} // namespace glt::preflight
