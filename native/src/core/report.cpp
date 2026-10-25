#include "glt/core/report.hpp"

#include "glt/core/backups.hpp"
#include "glt/core/db.hpp"
#include "glt/core/disk.hpp"
#include "glt/core/hosts.hpp"
#include "glt/core/keymaps.hpp"
#include "glt/core/log.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/perf.hpp"
#include "glt/core/procs.hpp"
#include "glt/core/registry.hpp"
#include "glt/core/strings.hpp"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace glt::report {
namespace {

using json = nlohmann::json;

constexpr std::size_t kMaxReportBytes = 200'000;

void line(std::ostringstream& os, const std::string& s) { os << s << '\n'; }

// Every path in this module is std::wstring (the Win32 convention), so the
// helper converts on the way out rather than forcing each caller to.
std::string or_unknown(const std::wstring& v) {
    return v.empty() ? std::string("NOT FOUND") : str::to_utf8(v);
}

std::string reg_value_text(const reg::RegValue& v) {
    return v ? std::to_string(*v) : std::string("unset");
}

json alloc_to_json(const reg::PerfAllocation& a) {
    json j = json::object();
    j["PerformanceMode"] = a.performance_mode ? json(*a.performance_mode) : json(nullptr);
    j["PerformanceMemoryGb"] = a.memory_gb ? json(*a.memory_gb) : json(nullptr);
    j["PerformanceCpuCoreNum"] = a.cpu_cores ? json(*a.cpu_cores) : json(nullptr);
    j["RenderMode"] = a.render_mode ? json(*a.render_mode) : json(nullptr);
    j["RenderUserType"] = a.render_user_type ? json(*a.render_user_type) : json(nullptr);
    return j;
}

// {path → {name → value}} with sensitive keys removed at every level.
json branch_to_json(std::wstring_view path) {
    json out = json::object();
    for (const auto& [sub, values] : reg::export_branch(path)) {
        json node = json::object();
        for (const auto& [name, v] : values) {
            if (is_sensitive_key(str::to_utf8(name))) continue;
            node[str::to_utf8(name)] = v ? json(*v) : json(nullptr);
        }
        out[str::to_utf8(sub)] = std::move(node);
    }
    return out;
}

} // namespace

bool is_sensitive_key(const std::string& key) {
    // report.py::SKIP_REG_KEYS, carried over verbatim. These are dropped
    // entirely rather than masked, because a masked value still invites a
    // follow-up question in a public thread.
    static const char* kSkip[] = {
        "svid",        "SyzsGuid",      "cert_hash",     "VPoDioa",
        "VOPoDioa",    "VWiDioa",       "ams_union_id_v2", "cert_path",
        "ip_latitude", "ip_longitude",
    };
    for (const char* s : kSkip) {
        if (str::iequals(key, s)) return true;
    }
    return false;
}

std::string build() {
    std::ostringstream os;

    line(os, std::string("GameLoop Tuner diagnostic report v") + kVersion);
    line(os, "generated: " + str::timestamp_readable());

    {
        const auto host = procs::host_resources();
        line(os, "host: cores=" + std::to_string(host.cores) +
                     " mem_gb=" + std::to_string(host.mem_gb));
        const auto hi = paths::host_info();
        line(os, "os: " + str::to_utf8(hi.machine) + " " + str::to_utf8(hi.version));
    }
    line(os, "");

    // ── paths ──────────────────────────────────────────────────────────────
    const std::wstring root = paths::install_root();
    const std::wstring dbp = paths::appdata_db();

    line(os, "[paths]");
    line(os, "install=" + or_unknown(root));
    line(os, "db=" + std::string(dbp.empty() ? "NOT FOUND" : "present"));
    if (!dbp.empty()) {
        line(os, "db_path=" + str::to_utf8(dbp));
        line(os, "db_bytes=" + std::to_string(str::file_size(dbp)));
        line(os, "wal_bytes=" + std::to_string(str::file_size(dbp + L"-wal")));
    }
    line(os, "smk_conf=" + or_unknown(paths::smk_conf()));
    line(os, "build=" + or_unknown(paths::app_version_dir(root)));
    line(os, "adb=" + std::string(paths::adb_exe(root).empty() ? "missing" : "present"));
    line(os, "ano_tmp=" + or_unknown(paths::ano_tmp_dir()));
    line(os, "");

    // ── processes ──────────────────────────────────────────────────────────
    line(os, "[processes]");
    for (const auto& p : procs::list_gameloop()) {
        line(os, str::to_utf8(p.name) + " pid=" + std::to_string(p.pid) + " mem_kb=" +
                     std::to_string(p.mem_kb) + " ppid=" + std::to_string(p.parent_pid));
    }
    {
        const auto svc = procs::service_status();
        line(os, "service_state=" + str::to_utf8(svc.state_text()) + " (" +
                     std::to_string(svc.current_state) + ")");
        line(os, "service_start=" + str::to_utf8(svc.start_type_text()));
    }
    line(os, "");

    // ── perf ───────────────────────────────────────────────────────────────
    line(os, "[perf]");
    try {
        line(os, alloc_to_json(reg::perf_allocation()).dump());
        line(os, "common=" + branch_to_json(reg::kCommon).dump());
        line(os, "launcher=" + branch_to_json(reg::kSettingsLauncher).dump());
    } catch (const std::exception& e) {
        line(os, std::string("registry unreadable: ") + e.what());
    }
    line(os, "");

    // ── database ───────────────────────────────────────────────────────────
    if (!dbp.empty()) {
        try {
            db::Connection con(dbp, 5000);

            line(os, "[db]");
            for (const auto& t : db::triggers(con)) {
                line(os, "trigger " + t.name + " on " + t.table);
            }
            {
                const auto guarded = db::guarded_keys(con);
                line(os, "guarded_keys=" + std::to_string(guarded.size()));
                for (const auto& g : guarded) line(os, "  " + g);
            }
            line(os, "");

            line(os, "[keymaps]");
            const auto dr = keymaps::drift_report(con, paths::kPkg);
            line(os, "lastmodeid=" + (dr.lastmodeid ? *dr.lastmodeid : std::string("?")));
            line(os, "mode145_solution=" +
                         (dr.mode145_solution ? *dr.mode145_solution : std::string("?")));
            line(os, "recentlyused=" +
                         str::scrub_secrets(dr.recentlyused ? *dr.recentlyused : ""));

            for (const auto& s : keymaps::solutions(con, paths::kPkg)) {
                line(os, "layout mode=" + s.mode + " sol=" + s.sol + " name=" + s.name +
                             " switchRules=" + std::to_string(s.rule_count));
            }
            {
                const auto jumps = keymaps::jump_tags(con, paths::kPkg);
                line(os, "jump ModeIDs=" + std::to_string(jumps.size()));
                for (const auto& j : jumps) line(os, "  " + j);
            }
            line(os, "");

            line(os, "[codm]");
            const std::string uc = perf::local_user_config_raw(con, paths::kPkg);
            try {
                auto j = json::parse(uc);
                for (const auto* k : {"frame_rate", "clarity", "low_latency_mode",
                                      "default_frame_gen_on", "aspect_ratio",
                                      "is_toolbar_open", "hide_full_screen_topbar"}) {
                    const auto it = j.find(k);
                    line(os, std::string(k) + "=" +
                                 (it == j.end() ? std::string("(unset)") : it->dump()));
                }
            } catch (...) {
                line(os, "local_user_config unparseable (" +
                             std::to_string(uc.size()) + " bytes)");
            }

            const std::string hc = perf::host_config_raw(con, paths::kPkg);
            try {
                auto j = json::parse(hc);
                const auto d = j.find("default_frame_rate");
                const auto m = j.find("maximum_frame_rate");
                line(os, "host_fps=" +
                             (d == j.end() ? std::string("?") : d->dump()) + "/" +
                             (m == j.end() ? std::string("?") : m->dump()));
            } catch (...) {
                line(os, "host_config unparseable");
            }

            {
                if (auto smk = keymaps::read_smk(con, paths::kPkg)) {
                        const auto ids = keymaps::tex_ids_in_text(*smk);
                        std::string blind = "none present";
                        for (const int id : {10004, 10046, 30004, 30046}) {
                            if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
                                blind = std::to_string(id) + ",";
                            }
                        }
                        line(os, "smk_db_templates=" + std::to_string(ids.size()) +
                                     " blind_present=" + blind +
                                     " (dormant mirror - the smk.conf FILE is operative)");
                    }
            }
            line(os, "");
        } catch (const std::exception& e) {
            line(os, std::string("db unreadable: ") + e.what());
            line(os, "");
        }
    }

    // ── smk.conf file ──────────────────────────────────────────────────────
    line(os, "[smk_file]");
    {
        const std::wstring smk = paths::smk_conf();
        if (smk.empty()) {
            line(os, "not found");
        } else {
            line(os, "bytes=" + std::to_string(str::file_size(smk)));
            line(os, "readonly=" + std::string(str::is_readonly(smk) ? "YES" : "no"));

            std::string text;
            if (str::read_file(smk, text)) {
                const auto ids = keymaps::tex_ids_in_text(text);
                line(os, "tex_templates=" + std::to_string(ids.size()));
                for (const int id : {10004, 10046, 30004, 30046}) {
                    const bool present =
                        std::find(ids.begin(), ids.end(), id) != ids.end();
                    line(os, "  Tex " + std::to_string(id) +
                                 (present ? " PRESENT (jump path live)" : " absent"));
                }
                line(os, std::string("valid_utf8=") + (str::is_valid_utf8(text) ? "yes" : "no"));
                line(os, std::string("crlf_count=") + std::to_string(
                              (std::size_t)std::count(text.begin(), text.end(), '\r')));
            }
        }
    }
    line(os, "");

    // ── disk ───────────────────────────────────────────────────────────────
    line(os, "[disk]");
    {
        const auto g = disk::game_drive_status(root);
        char buf[128]{};
        // snprintf_s takes the buffer SIZE as its second argument — the
        // (buf, "fmt", …) shape is sprintf_s, and mixing them up is a
        // compile error rather than a subtle bug.
        std::snprintf(buf, sizeof(buf), "drive=%s free=%.1fGB min=%.0fGB ok=%s",
                   str::to_utf8(g.drive).c_str(), g.free_gb, g.min_gb, g.ok ? "yes" : "NO");
        line(os, buf);

        for (const auto& [label, bytes] : disk::footprint(root)) {
            std::snprintf(buf, sizeof(buf), "size %s=%.2fGB", label.c_str(),
                       static_cast<double>(bytes) / 1e9);
            line(os, buf);
        }
    }
    line(os, "");

    // ── backups ────────────────────────────────────────────────────────────
    line(os, "[backups]");
    for (const auto& b : backups::list_backups(dbp)) {
        line(os, str::to_utf8(b.filename) + " " + std::to_string(b.bytes) + "B");
    }
    line(os, "");

    // ── hosts ──────────────────────────────────────────────────────────────
    line(os, "[hosts]");
    line(os, std::string("pckeymap_blocked=") +
                 (hosts::is_blocked() ? "YES" : "no"));
    line(os, "");

    // ── log tail ───────────────────────────────────────────────────────────
    line(os, "[tuner log tail]");
    {
        const auto lines = log::mem_lines();
        const std::size_t n = lines.size() > 80 ? lines.size() - 80 : 0;
        for (std::size_t i = n; i < lines.size(); ++i) line(os, lines[i]);
    }

    std::string out = str::scrub_secrets(os.str());

    // Hard cap. A runaway log tail must never produce an unattachable file.
    if (out.size() > kMaxReportBytes) {
        out.resize(kMaxReportBytes);
        out += "\n... truncated at 200 KB ...\n";
    }
    return out;
}

std::string export_to_desktop() {
    std::wstring desktop;

    // FOLDERID_Desktop is per-user and, unlike %USERPROFILE%\Desktop, resolves
    // correctly when OneDrive has redirected it. The literal folder is only a
    // fallback for a profile with no known-folder support.
    PWSTR raw = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &raw))) {
        desktop = raw;
        ::CoTaskMemFree(raw);
    } else {
        PWSTR profile = nullptr;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile))) {
            desktop = str::path_join(profile, L"Desktop");
            ::CoTaskMemFree(profile);
        }
    }
    if (desktop.empty()) return {};
    str::create_dirs(desktop);

    const std::wstring path =
        str::path_join(desktop, L"GameLoopTuner-report-" + str::timestamp_compact_w() + L".txt");
    if (!str::write_file(path, build())) return {};

    log::info("diagnostic report written: " + str::to_utf8(path));
    return str::to_utf8(path);
}

} // namespace glt::report