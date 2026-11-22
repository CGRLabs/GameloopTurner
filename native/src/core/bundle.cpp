// glt/core/bundle.cpp — see glt/core/bundle.hpp for the design notes.
#include "glt/core/bundle.hpp"

#include "glt/core/backups.hpp"
#include "glt/core/db.hpp"
#include "glt/core/keymaps.hpp"
#include "glt/core/log.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/perf.hpp"
#include "glt/core/registry.hpp"
#include "glt/core/strings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <windows.h>

#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace glt::bundle {
namespace {

using json = nlohmann::json;

constexpr int kFormatVersion = 1;
constexpr std::string_view kFormatName = "gltbak";

// ── SHA-256 via CNG (bcrypt) ─────────────────────────────────────────────────

std::string sha256_hex(std::string_view data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string out;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};

    DWORD hash_len = 0;
    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hash_len),
                          sizeof(hash_len), &hash_len, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    std::vector<UCHAR> hash_buf(hash_len);
    std::vector<UCHAR> hash_object;
    DWORD obj_len = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&obj_len),
                          sizeof(obj_len), &obj_len, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    hash_object.resize(obj_len);

    if (BCryptCreateHash(alg, &hash, hash_object.data(), obj_len, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    if (BCryptHashData(hash,
                       reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                       static_cast<ULONG>(data.size()), 0) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }
    if (BCryptFinishHash(hash, hash_buf.data(), static_cast<ULONG>(hash_buf.size()), 0) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return {};
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);

    out.reserve(hash_buf.size() * 2);
    for (UCHAR byte : hash_buf) {
        out.push_back("0123456789abcdef"[byte >> 4]);
        out.push_back("0123456789abcdef"[byte & 0x0F]);
    }
    return out;
}

// Canonical serialization: sorted keys, compact, no whitespace, so the digest
// is stable across writes.
std::string canonical_json(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

// ── Registry helpers ─────────────────────────────────────────────────────────

RegValue::Type type_from_reg(DWORD type) {
    switch (type) {
        case REG_DWORD:   return RegValue::Type::Dword;
        case REG_QWORD:   return RegValue::Type::Qword;
        case REG_SZ:
        case REG_EXPAND_SZ:
        case REG_MULTI_SZ: return RegValue::Type::String;
        case REG_BINARY:   return RegValue::Type::Binary;
        default:           return RegValue::Type::Unknown;
    }
}

json reg_value_to_json(const RegValue& v) {
    switch (v.type) {
        case RegValue::Type::Dword:
        case RegValue::Type::Qword:
            return v.i;
        case RegValue::Type::String:
            return v.s;
        case RegValue::Type::Binary: {
            json arr = json::array();
            for (std::uint8_t byte : v.b) arr.push_back(static_cast<int>(byte));
            return arr;
        }
        default:
            return nullptr;
    }
}

std::string reg_value_text(const RegValue& v) {
    switch (v.type) {
        case RegValue::Type::Dword:
        case RegValue::Type::Qword:
            return std::to_string(v.i);
        case RegValue::Type::String:
            return v.s;
        case RegValue::Type::Binary:
            return "<binary " + std::to_string(v.b.size()) + " bytes>";
        default:
            return "<unknown>";
    }
}

std::optional<RegBranch> read_reg_branch(std::wstring_view path) {
    HKEY k = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, path.data(), 0, KEY_READ, &k) != ERROR_SUCCESS)
        return std::nullopt;

    // Query how many values and the max name/data sizes.
    DWORD value_count = 0, max_name_len = 0, max_data_len = 0;
    if (::RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                           &value_count, &max_name_len, &max_data_len, nullptr, nullptr) != ERROR_SUCCESS) {
        ::RegCloseKey(k);
        return std::nullopt;
    }

    RegBranch branch;
    branch.path = std::wstring(path);

    if (value_count == 0) {
        ::RegCloseKey(k);
        return branch;
    }

    std::vector<wchar_t> name_buf(max_name_len + 1);
    std::vector<BYTE> data_buf(max_data_len);

    for (DWORD i = 0; i < value_count; ++i) {
        DWORD name_len = static_cast<DWORD>(name_buf.size());
        DWORD data_len = static_cast<DWORD>(data_buf.size());
        DWORD type = 0;

        if (::RegEnumValueW(k, i, name_buf.data(), &name_len, nullptr, &type,
                            data_buf.data(), &data_len) != ERROR_SUCCESS) {
            continue;
        }

        RegValue rv;
        switch (type) {
            case REG_DWORD:
                rv.type = RegValue::Type::Dword;
                rv.i = *reinterpret_cast<std::uint32_t*>(data_buf.data());
                break;
            case REG_QWORD:
                rv.type = RegValue::Type::Qword;
                rv.i = *reinterpret_cast<std::uint64_t*>(data_buf.data());
                break;
            case REG_SZ:
            case REG_EXPAND_SZ:
            case REG_MULTI_SZ: {
                rv.type = RegValue::Type::String;
                // null-terminated wide string(s)
                rv.s = str::to_utf8(std::wstring_view(reinterpret_cast<const wchar_t*>(data_buf.data()),
                                                      data_len / sizeof(wchar_t)));
                break;
            }
            case REG_BINARY: {
                rv.type = RegValue::Type::Binary;
                rv.b.assign(data_buf.data(), data_buf.data() + data_len);
                break;
            }
            default:
                continue; // skip unknown types
        }
        branch.values[str::to_utf8(std::wstring_view(name_buf.data(), name_len))] = std::move(rv);
    }

    ::RegCloseKey(k);
    return branch;
}

bool write_reg_value(std::wstring_view path, std::wstring_view value_name,
                     const RegValue& rv) {
    HKEY k = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, path.data(), 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &k,
                          nullptr) != ERROR_SUCCESS) {
        return false;
    }

    LONG rc = ERROR_SUCCESS;
    switch (rv.type) {
        case RegValue::Type::Dword: {
            std::uint32_t v = static_cast<std::uint32_t>(rv.i);
            rc = ::RegSetValueExW(k, value_name.data(), 0, REG_DWORD,
                                  reinterpret_cast<const BYTE*>(&v), sizeof(v));
            break;
        }
        case RegValue::Type::Qword: {
            rc = ::RegSetValueExW(k, value_name.data(), 0, REG_QWORD,
                                  reinterpret_cast<const BYTE*>(&rv.i), sizeof(rv.i));
            break;
        }
        case RegValue::Type::String: {
            rc = ::RegSetValueExW(k, value_name.data(), 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(rv.s.data()),
                                  static_cast<DWORD>((rv.s.size() + 1) * sizeof(char)));
            break;
        }
        case RegValue::Type::Binary: {
            rc = ::RegSetValueExW(k, value_name.data(), 0, REG_BINARY,
                                  rv.b.data(), static_cast<DWORD>(rv.b.size()));
            break;
        }
        default:
            rc = ERROR_INVALID_DATA;
            break;
    }

    ::RegCloseKey(k);
    return rc == ERROR_SUCCESS;
}

// ── Source system capture ────────────────────────────────────────────────────

std::wstring read_reg_string(HKEY root, std::wstring_view subkey,
                             std::wstring_view value_name) {
    HKEY k = nullptr;
    if (::RegOpenKeyExW(root, subkey.data(), 0, KEY_READ, &k) != ERROR_SUCCESS)
        return {};
    wchar_t buf[512]{};
    DWORD cb = static_cast<DWORD>(sizeof(buf));
    LONG rc = ::RegGetValueW(k, nullptr, value_name.data(),
                             RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buf, &cb);
    ::RegCloseKey(k);
    if (rc != ERROR_SUCCESS) return {};
    return std::wstring(buf);
}

SourceSystem capture_source_system() {
    SourceSystem sys;

    wchar_t hostname_buf[256]{};
    DWORD hostname_len = static_cast<DWORD>(sizeof(hostname_buf) / sizeof(hostname_buf[0]));
    if (::GetComputerNameW(hostname_buf, &hostname_len))
        sys.hostname = str::to_utf8(std::wstring_view(hostname_buf, hostname_len));

    auto hi = paths::host_info();
    sys.windows_version = str::to_utf8(hi.version);
    sys.arch = str::to_utf8(hi.machine);

    // GameLoop version: HKLM root first, then HKCU\...\GameLoop.
    auto hkcm_ver = read_reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Tencent\\GameLoop", L"Version");
    if (hkcm_ver.empty())
        hkcm_ver = read_reg_string(HKEY_CURRENT_USER, L"Software\\Tencent\\GameLoop\\GameLoop", L"Version");
    sys.gameloop_version = str::to_utf8(hkcm_ver);

    // Engine version from HKCU\...\GameLoop\Image\Version.
    auto eng = read_reg_string(HKEY_CURRENT_USER, L"Software\\Tencent\\GameLoop\\Image", L"Version");
    sys.engine_version = str::to_utf8(eng);

    // CODM version: from InstalledApp.db if present.
    const std::wstring db_dir = paths::db_dir();
    if (!db_dir.empty()) {
        const std::wstring installed_app = str::path_join(db_dir, L"InstalledApp.db");
        if (str::file_exists(installed_app)) {
            // Lightweight: just try a sqlite query. If it fails, leave empty.
            // We can't easily link sqlite here, so skip—CODM version is nice-to-have.
        }
    }

    return sys;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// read_bundle / write_bundle
// ─────────────────────────────────────────────────────────────────────────────

std::optional<Bundle> read_bundle(const std::wstring& path) {
    std::string text;
    if (!str::read_file(path, text)) return std::nullopt;

    json j;
    try {
        j = json::parse(text);
    } catch (...) {
        return std::nullopt;
    }

    if (!j.contains("format") || !j.contains("sha256")) return std::nullopt;
    if (j["format"].get<int>() != kFormatVersion) return std::nullopt;

    // Verify digest: recompute over the bundle with sha256 removed.
    json verify = j;
    verify.erase("sha256");
    std::string digest = sha256_hex(canonical_json(verify));
    if (digest != j["sha256"].get<std::string>()) {
        log::warn("bundle digest mismatch - file may be corrupt or tampered");
        return std::nullopt;
    }

    Bundle b;
    b.format = j["format"].get<int>();
    b.tuner_version = j.value("tuner_version", std::string{});
    b.created = j.value("created", std::string{});
    b.title = j.value("title", std::string{});

    if (j.contains("source_system")) {
        auto& ss = j["source_system"];
        b.source_system.hostname = ss.value("hostname", std::string{});
        b.source_system.windows_version = ss.value("windows_version", std::string{});
        b.source_system.gameloop_version = ss.value("gameloop_version", std::string{});
        b.source_system.engine_version = ss.value("engine_version", std::string{});
        b.source_system.codm_version = ss.value("codm_version", std::string{});
        b.source_system.arch = ss.value("arch", std::string{});
    }

    b.configs_db = j.value("sections", json::object()).value("configs_db", json::object());
    if (j.contains("sections") && j["sections"].contains("smk_conf"))
        b.smk_conf = j["sections"]["smk_conf"].get<std::string>();

    b.summary = j.value("summary", json::object());
    b.sha256 = j["sha256"].get<std::string>();

    // Registry branches: sections.registry is an array of {path, values}.
    if (j.contains("sections") && j["sections"].contains("registry")) {
        for (const auto& br : j["sections"]["registry"]) {
            RegBranch rb;
            rb.path = str::to_wide(br.value("path", std::string{}));
            for (const auto& [val_name, val] : br["values"].items()) {
                RegValue rv;
                rv.type = RegValue::Type::Qword;
                rv.i = val.is_number() ? val.get<std::int64_t>() : 0;
                rv.s = val.is_string() ? val.get<std::string>() : std::string{};
                rb.values[val_name] = std::move(rv);
            }
            b.registry.push_back(std::move(rb));
        }
    }

    return b;
}

bool write_bundle(const std::wstring& path, const Bundle& bundle) {
    json j;
    j["format"] = bundle.format;
    j["tuner_version"] = bundle.tuner_version;
    j["created"] = bundle.created;
    j["title"] = bundle.title;

    j["source_system"]["hostname"] = bundle.source_system.hostname;
    j["source_system"]["windows_version"] = bundle.source_system.windows_version;
    j["source_system"]["gameloop_version"] = bundle.source_system.gameloop_version;
    j["source_system"]["engine_version"] = bundle.source_system.engine_version;
    j["source_system"]["codm_version"] = bundle.source_system.codm_version;
    j["source_system"]["arch"] = bundle.source_system.arch;

    json sections;
    sections["configs_db"] = bundle.configs_db;
    sections["smk_conf"] = bundle.smk_conf;

    json registry_arr = json::array();
    for (const auto& br : bundle.registry) {
        json br_j;
        br_j["path"] = str::to_utf8(br.path);
        json values = json::object();
        for (const auto& [name, val] : br.values) {
            values[name] = reg_value_to_json(val);
        }
        br_j["values"] = values;
        registry_arr.push_back(std::move(br_j));
    }
    sections["registry"] = std::move(registry_arr);
    j["sections"] = std::move(sections);

    j["summary"] = bundle.summary;

    // Compute digest over everything except sha256.
    json for_digest = j;
    for_digest.erase("sha256");
    std::string digest = sha256_hex(canonical_json(for_digest));
    j["sha256"] = digest;

    return str::write_file(path, canonical_json(j));
}

// ─────────────────────────────────────────────────────────────────────────────
// capture_bundle
// ─────────────────────────────────────────────────────────────────────────────

Bundle capture_bundle(std::string_view title) {
    Bundle b;
    b.format = kFormatVersion;
    b.tuner_version = GLT_VERSION;
    b.created = str::timestamp_readable();
    b.title = std::string(title);

    const std::wstring dbp = paths::appdata_db();
    if (!dbp.empty()) {
        db::Connection con(dbp, 5000);

        // Capture every CODM keymap/perf row from both tables.
        const std::string pkg(paths::kPkg);

        // configs table: androws.<pkg>.* rows.
        auto dump_table = [&](const std::string& table) {
            json out = json::object();
            try {
                for (const auto& row : con.query("SELECT key, value FROM " + table)) {
                    std::string key = row.at(0).as_text();
                    // Only keep CODM rows.
                    if (key.find("androws." + pkg + ".") != 0) continue;
                    try {
                        out[key] = json::parse(row.at(1).as_text());
                    } catch (...) {
                        out[key] = row.at(1).as_text(); // raw if not JSON
                    }
                }
            } catch (...) {
                // table may not exist on this build
            }
            return out;
        };

        b.configs_db = dump_table("configs");

        // keymap cache: keymap_mode.* and keymap_data.* rows for CODM.
        try {
            for (const auto& row :
                 con.query("SELECT key, value0 FROM key_map_configs_cache_official")) {
                std::string key = row.at(0).as_text();
                if (key.find(pkg) == std::string::npos) continue;
                try {
                    b.configs_db[key] = json::parse(row.at(1).as_text());
                } catch (...) {
                    b.configs_db[key] = row.at(1).as_text();
                }
            }
        } catch (...) {}
    }

    // smk.conf
    const std::wstring smk = paths::smk_conf();
    if (!smk.empty()) {
        std::string text;
        if (str::read_file(smk, text)) b.smk_conf = std::move(text);
    }

    // Registry branches we care about.
    const std::vector<std::wstring_view> reg_paths = {
        L"Software\\Tencent\\GameLoop\\Settings\\Common",
        L"Software\\Tencent\\GameLoop\\Settings\\GameLoop",
        L"Software\\Tencent\\GameLoop\\Env",
        L"Software\\Tencent\\GameLoop\\Image",
    };
    for (auto& p : reg_paths) {
        if (auto br = read_reg_branch(p)) b.registry.push_back(std::move(*br));
    }

    b.source_system = capture_source_system();

    // Summary for the preview UI.
    b.summary["keymap_mode"] = b.configs_db.value("keymap_mode.unlogin." + std::string(paths::kPkg) + ".145",
                                                   json::object()).value("lastSolutionId", std::string{"unknown"});
    b.summary["lastmodeid"] = b.configs_db.value("lastmodeid.unlogin." + std::string(paths::kPkg),
                                                 std::string{"unknown"});
    if (b.configs_db.contains("androws." + std::string(paths::kPkg) + ".local_user_config"))
        b.summary["clarity"] = b.configs_db["androws." + std::string(paths::kPkg) + ".local_user_config"]
                                   .value("clarity", json());
    if (b.configs_db.contains("androws." + std::string(paths::kPkg) + ".local_user_config"))
        b.summary["low_latency_mode"] =
            b.configs_db["androws." + std::string(paths::kPkg) + ".local_user_config"]
                .value("low_latency_mode", json());
    b.summary["smk_readonly"] = str::is_readonly(smk);

    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// diff_bundle
// ─────────────────────────────────────────────────────────────────────────────

std::vector<FieldDiff> diff_bundle(const Bundle& bundle) {
    std::vector<FieldDiff> diffs;

    const std::wstring dbp = paths::appdata_db();
    std::unique_ptr<db::Connection> con;
    if (!dbp.empty()) {
        try {
            con = std::make_unique<db::Connection>(dbp, 5000);
        } catch (...) {
            con = nullptr;
        }
    }

    // Compare configs_db sections.
    for (auto it = bundle.configs_db.begin(); it != bundle.configs_db.end(); ++it) {
        const std::string& key = it.key();
        FieldDiff d;
        d.section = "configs_db";
        d.key = key;
        d.backup_value = it.value().dump(-1, ' ', false);
        d.matches = false;

        if (con) {
            try {
                std::vector<db::Value> params;
                params.emplace_back(key);
                if (auto v = con->query_one("SELECT value FROM configs WHERE key=?", params)) {
                    d.live_value = v->as_text();
                    // Normalize: try parsing both as JSON for comparison.
                    try {
                        auto backup_j = json::parse(d.backup_value);
                        auto live_j = json::parse(d.live_value);
                        d.matches = (backup_j == live_j);
                    } catch (...) {
                        d.matches = (d.backup_value == d.live_value);
                    }
                } else {
                    d.live_value = "<missing on this system>";
                }
            } catch (...) {
                d.live_value = "<error reading>";
            }
        } else {
            d.live_value = "<Configs.db unreadable>";
        }
        diffs.push_back(std::move(d));
    }

    // smk.conf
    {
        FieldDiff d;
        d.section = "smk_conf";
        d.key = "smk.conf";
        d.backup_value = bundle.smk_conf.empty() ? "<empty>" : bundle.smk_conf;
        d.matches = false;

        const std::wstring smk = paths::smk_conf();
        if (!smk.empty()) {
            std::string text;
            if (str::read_file(smk, text)) {
                d.live_value = text;
                d.matches = (d.backup_value == d.live_value);
            } else {
                d.live_value = "<unreadable>";
            }
        } else {
            d.live_value = "<not found>";
        }
        diffs.push_back(std::move(d));
    }

    // Registry branches
    for (const auto& br : bundle.registry) {
        if (con) {
            try {
                auto live_values = reg::read_all(br.path);
                std::unordered_map<std::string, std::int64_t> live_map;
                for (const auto& [name, val] : live_values)
                    live_map[str::to_utf8(name)] = val ? *val : 0;

                for (const auto& [val_name, val] : br.values) {
                    FieldDiff d;
                    d.section = "registry";
                    d.key = str::to_utf8(br.path) + "\\" + val_name;
                    d.backup_value = reg_value_text(val);
                    auto live_it = live_map.find(val_name);
                    if (live_it == live_map.end()) {
                        d.live_value = "<missing on this system>";
                        d.matches = false;
                    } else {
                        d.live_value = std::to_string(live_it->second);
                        d.matches = (val.i == live_it->second);
                    }
                    diffs.push_back(std::move(d));
                }
            } catch (...) {}
        }
    }

    return diffs;
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_bundle
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string> apply_bundle(const Bundle& bundle, ApplyOptions options,
                                      bool dry_run) {
    std::vector<std::string> notes;

    if (dry_run) {
        notes.push_back("DRYRUN: bundle would be applied to this system");
        auto diffs = diff_bundle(bundle);
        std::size_t changes = 0;
        for (const auto& d : diffs)
            if (!d.matches) ++changes;
        notes.push_back(std::to_string(diffs.size()) + " fields compared, " +
                        std::to_string(changes) + " would change");
        return notes;
    }

    const std::wstring dbp = paths::appdata_db();
    if (dbp.empty()) {
        notes.push_back("apply failed: Configs.db not found - is GameLoop installed?");
        return notes;
    }

    // ── Auto-backup before apply (undo snapshot) ─────────────────────────────
    {
        auto safety = capture_bundle("Auto-backup before apply: " + bundle.title);
        const std::wstring tmp_dir = paths::temp_dir();
        if (!tmp_dir.empty()) {
            std::wstring safety_path = str::path_join(tmp_dir, L"gameloop-tuner");
            str::create_dirs(safety_path);
            safety_path = str::path_join(safety_path, L"pre-apply-backup-" +
                                          str::timestamp_compact_w() + L".gltbak");
            if (write_bundle(safety_path, safety)) {
                notes.push_back("Auto-backup saved before apply: " +
                                str::to_utf8(safety_path));
            } else {
                notes.push_back("Auto-backup save failed - proceeding anyway.");
            }
        }
    }

    // ── Version mismatch warning ─────────────────────────────────────────────
    {
        const auto live = capture_source_system();
        auto warn_if_diff = [&](const char* label, std::string_view bundle_v,
                                 std::string_view live_v) {
            if (!bundle_v.empty() && !live_v.empty() && bundle_v != live_v)
                notes.push_back("Version mismatch [" + std::string(label) +
                                "]: bundle=" + std::string(bundle_v) +
                                " live=" + std::string(live_v));
        };
        warn_if_diff("GameLoop", bundle.source_system.gameloop_version, live.gameloop_version);
        warn_if_diff("Engine", bundle.source_system.engine_version, live.engine_version);
        warn_if_diff("CODM", bundle.source_system.codm_version, live.codm_version);
        warn_if_diff("Windows", bundle.source_system.windows_version, live.windows_version);
        warn_if_diff("Arch", bundle.source_system.arch, live.arch);
    }

    // ── configs_db writes (transactional, with backup) ───────────────────────
    if (options.configs_db) {
        std::vector<db::Op> ops;
        for (auto it = bundle.configs_db.begin(); it != bundle.configs_db.end(); ++it) {
            const std::string& key = it.key();
            db::Op op;
            op.sql = "INSERT OR REPLACE INTO configs(key, value, encrypt, json_data) VALUES(?, ?, 0, ?)";
            op.params = {db::Value(key), db::Value(it.value().dump()), db::Value(it.value().dump())};
            op.describe = "write " + key;
            ops.push_back(std::move(op));
        }

        // keymap cache rows
        for (auto it = bundle.configs_db.begin(); it != bundle.configs_db.end(); ++it) {
            const std::string& key = it.key();
            if (key.find("keymap_") != 0 && key.find("offcial_") != 0) continue;
            db::Op op;
            op.sql = "INSERT OR REPLACE INTO key_map_configs_cache_official(key, value0, value1) VALUES(?, ?, ?)";
            op.params = {db::Value(key), db::Value(it.value().dump()), db::Value{}};
            op.describe = "write keymap " + key;
            ops.push_back(std::move(op));
        }

        if (!ops.empty()) {
            auto res = db::guarded_updates(dbp, ops, false);
            notes.insert(notes.end(), res.notes.begin(), res.notes.end());
        }
    }

    // ── smk.conf write ───────────────────────────────────────────────────────
    if (options.smk_conf) {
        const std::wstring smk = paths::smk_conf();
        if (!smk.empty() && !bundle.smk_conf.empty()) {
            str::set_readonly(smk, false); // unlock the kill-switch temporarily
            if (str::write_file(smk, bundle.smk_conf)) {
                notes.push_back("smk.conf restored from bundle");
            } else {
                notes.push_back("smk.conf write failed");
            }
        }
    }

    // ── registry writes ──────────────────────────────────────────────────────
    if (options.registry) {
        for (const auto& br : bundle.registry) {
            for (const auto& [val_name, val] : br.values) {
                if (!write_reg_value(br.path, str::to_wide(val_name), val)) {
                    notes.push_back("registry write failed: " + str::to_utf8(br.path) +
                                    "\\" + val_name);
                }
            }
        }
        if (!bundle.registry.empty())
            notes.push_back("registry branches restored from bundle");
    }

    log::info("bundle applied: " + bundle.title);
    return notes;
}

// ── File association (.gltbak) ────────────────────────────────────────────

namespace {

std::wstring module_path() {
    wchar_t buf[MAX_PATH]{};
    if (::GetModuleFileNameW(nullptr, buf, MAX_PATH) == 0) return {};
    return std::wstring(buf);
}

bool set_reg_string(HKEY root, std::wstring_view subkey,
                    std::wstring_view value_name, std::wstring_view value) {
    HKEY k = nullptr;
    if (::RegCreateKeyExW(root, subkey.data(), 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &k,
                          nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG rc = ::RegSetValueExW(k, value_name.data(), 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(value.data()),
                               static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(k);
    return rc == ERROR_SUCCESS;
}

} // namespace

bool bundle::register_file_association() {
    const std::wstring exe = module_path();
    if (exe.empty()) return false;

    // Command: "C:\...\GameLoopTuner.exe" "%1"
    std::wstring cmd = L"\"" + exe + L"\" \"%1\"";

    // ProgID: GameloopTuner.Bundle
    const std::wstring progid = L"GameloopTuner.Bundle";
    const std::wstring progid_desc = L"GameloopTuner Backup Bundle";

    // 1. .gltbak -> ProgID
    if (!set_reg_string(HKEY_CURRENT_USER, L"Software\\Classes\\.gltbak", L"", progid))
        return false;

    // 2. ProgID description
    if (!set_reg_string(HKEY_CURRENT_USER, L"Software\\Classes\\" + progid, L"", progid_desc))
        return false;

    // 3. Default icon — resource ID 2 is gltbak.ico inside this exe
    if (!set_reg_string(HKEY_CURRENT_USER,
                        L"Software\\Classes\\" + progid + L"\\DefaultIcon", L"",
                        exe + L",2"))
        return false;

    // 4. Open command
    if (!set_reg_string(HKEY_CURRENT_USER,
                        L"Software\\Classes\\" + progid + L"\\shell\\open\\command", L"", cmd))
        return false;

    log::info("registered .gltbak file association");
    return true;
}

} // namespace glt::bundle
