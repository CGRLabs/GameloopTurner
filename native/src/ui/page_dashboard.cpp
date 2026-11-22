// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_dashboard.cpp — Windows 11 Native Dashboard Overview
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/page_dashboard.hpp"

#include "glt/core/keymaps.hpp"
#include "glt/core/i18n.hpp"
#include "glt/core/netinfo.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/strings.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"

#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace glt::ui {
namespace {

using glt::i18n::tr;

using json = nlohmann::json;

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

void card_gameloop(App& app, const App::Dashboard& d) {
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

    ImGui::BeginChild("##card_gl_status", ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    const bool running = !d.procs.empty();

    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(tr("GameLoop Engine Status"));
    ImGui::PopStyleColor();
    ImGui::PopFont();


    ImGui::SameLine(0, 10.0f);
    if (running) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), tr("RUNNING (%d PIDs)"), static_cast<int>(d.procs.size()));
        widgets::status_pill(buf, col(theme::kOk), col(theme::kOkBg));
    } else {
        widgets::status_pill(tr("NOT RUNNING"), col(theme::kTextMuted), col(theme::kInput));
    }

    ImGui::Dummy(ImVec2(0, 4));

    if (d.procs.empty()) {
        widgets::body_text(tr("No active GameLoop processes detected. Launch the emulator to begin tuning."),
                           avail_w - 32.0f, col(theme::kTextDim));
    } else {
        for (std::size_t i = 0; i < d.procs.size() && i < 4; ++i) {
            const auto& p = d.procs[i];
            char buf[128];
            std::snprintf(buf, sizeof(buf), tr("PID %u  \xE2\x80\xA2  %llu MB RAM"),
                          p.pid, static_cast<unsigned long long>(p.mem_kb / 1024));
            widgets::kv_row(str::to_utf8(p.name), buf);
        }
        if (d.procs.size() > 4) {
            char more_buf[64];
            std::snprintf(more_buf, sizeof(more_buf), tr("+%d background subprocesses"),
                          static_cast<int>(d.procs.size() - 4));
            widgets::kv_row_continued(more_buf);
        }
    }

    ImGui::Dummy(ImVec2(0, 6));

    // Action buttons inside card
    if (running) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kError));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
        const std::string terminate_label =
            std::string("\xE2\x9C\x95 ") + tr("Terminate GameLoop Process Tree");
        if (ImGui::Button(terminate_label.c_str(), ImVec2(240.0f, 26.0f))) {
            app.close_gameloop();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgActive));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kOk));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
        const std::string launch_label =
            std::string("\xE2\x96\xB6 ") + tr("Launch GameLoop");
        if (ImGui::Button(launch_label.c_str(), ImVec2(160.0f, 26.0f))) {
            app.launch_gameloop();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void card_allocation(const App::Dashboard& d, const App& app) {
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

    ImGui::BeginChild("##card_vm_alloc", ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(tr("Virtual Machine Resources"));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    widgets::help_marker(tr("How much of your PC's CPU and RAM GameLoop's VM gets. "
                         "Leave 1 core and 2 GB for Windows. Needs GameLoop restart."));

    ImGui::SameLine(0, 10.0f);
    widgets::status_pill(tr("REGISTRY SYNCED"), col(theme::kAccent), col(theme::kAccentDim));

    ImGui::Dummy(ImVec2(0, 6));

    const float cores_val = static_cast<float>(d.alloc.cpu_cores.value_or(0));
    const float cores_max = static_cast<float>(app.host().cores);
    widgets::gauge(tr("CPU Cores"), cores_val, cores_max, tr("Cores"));

    ImGui::Dummy(ImVec2(0, 4));

    const float mem_val = static_cast<float>(d.alloc.memory_gb.value_or(0));
    const float mem_max = static_cast<float>(app.host().mem_gb);
    widgets::gauge(tr("RAM (GB)"), mem_val, mem_max, "GB");

    ImGui::Dummy(ImVec2(0, 6));

    const auto val = [](const reg::RegValue& v) -> std::string {
        return v ? std::to_string(*v) : std::string(tr("unset"));
    };

    widgets::kv_row(tr("Render Backend"),
                    val(d.alloc.render_mode) + " - " +
                        str::to_utf8(reg::render_mode_label(d.alloc.render_mode)));
    widgets::kv_row(tr("Renderer Override"),
                    val(d.alloc.render_user_type) + " - " +
                        str::to_utf8(reg::render_user_label(d.alloc.render_user_type)));

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void card_codm_settings(const App::Dashboard& d) {
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

    ImGui::BeginChild("##card_codm", ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(tr("CODM Live Settings"));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    widgets::help_marker(tr("Live values read from Configs.db. Clarity = render resolution "
                         "(tier 2 recommended). Low-latency = smaller frame queue. "
                         "Frame-gen = fake frames (keep OFF)."));

    ImGui::Dummy(ImVec2(0, 6));

    static int s_clarity = -1;
    static bool s_low_latency = false;
    static bool s_frame_gen = false;
    static int s_frame_rate = -1;
    static bool s_has_clarity = false, s_has_ll = false, s_has_fg = false, s_has_fr = false;
    static std::string s_last;
    static std::chrono::steady_clock::time_point s_last_t{};
    if (s_last != d.local_user_config ||
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - s_last_t).count() >= 2) {
        s_last = d.local_user_config;
        s_last_t = std::chrono::steady_clock::now();
        s_clarity = -1; s_low_latency = false; s_frame_gen = false; s_frame_rate = -1;
        s_has_clarity = s_has_ll = s_has_fg = s_has_fr = false;
        if (!d.local_user_config.empty()) {
            try {
                const auto j = json::parse(d.local_user_config);
                if (j.contains("clarity") && j["clarity"].is_number()) {
                    s_clarity = j["clarity"].get<int>(); s_has_clarity = true;
                }
                if (j.contains("low_latency_mode") && j["low_latency_mode"].is_boolean()) {
                    s_low_latency = j["low_latency_mode"].get<bool>(); s_has_ll = true;
                }
                if (j.contains("default_frame_gen_on") && j["default_frame_gen_on"].is_boolean()) {
                    s_frame_gen = j["default_frame_gen_on"].get<bool>(); s_has_fg = true;
                }
                if (j.contains("frame_rate") && j["frame_rate"].is_number()) {
                    s_frame_rate = j["frame_rate"].get<int>(); s_has_fr = true;
                }
            } catch (...) {}
        }
    }
    int clarity = s_clarity;
    bool low_latency = s_low_latency;
    bool frame_gen = s_frame_gen;
    int frame_rate = s_frame_rate;
    bool has_clarity = s_has_clarity, has_ll = s_has_ll, has_fg = s_has_fg, has_fr = s_has_fr;

    char buf[64];
    if (has_clarity) {
        std::snprintf(buf, sizeof(buf), tr("Tier %d"), clarity);
        const ImVec4 c = (clarity == 2) ? col(theme::kOk) : col(theme::kTextPrimary);
        widgets::kv_row(tr("Clarity"), buf, &c);
    } else {
        widgets::kv_row(tr("Clarity"), tr("unset"));
    }
    if (has_ll) {
        ImVec4 llc = low_latency ? col(theme::kOk) : col(theme::kWarn);
        widgets::kv_row(tr("Low-latency"), low_latency ? tr("ON") : tr("OFF"), &llc);
    } else {
        widgets::kv_row(tr("Low-latency"), tr("unset"));
    }
    if (has_fg) {
        ImVec4 fgc = frame_gen ? col(theme::kWarn) : col(theme::kOk);
        widgets::kv_row(tr("Frame-gen"), frame_gen ? tr("ON") : tr("OFF"), &fgc);
    } else {
        widgets::kv_row(tr("Frame-gen"), tr("unset"));
    }
    if (has_fr) {
        std::snprintf(buf, sizeof(buf), tr("%d fps"), frame_rate);
        widgets::kv_row(tr("Frame rate"), buf);
    } else {
        widgets::kv_row(tr("Frame rate"), tr("unset"));
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void card_keymap(const App::Dashboard& d) {
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

    ImGui::BeginChild("##card_km_status", ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    bool active_flat = false;
    std::string active_name = "?";
    std::string active_rules = "?";
    const std::string lm = d.lastmodeid.value_or("?");

    for (const auto& s : d.solutions) {
        if (s.mode != lm) continue;
        if (lm == "145" && d.mode145_solution && s.sol == *d.mode145_solution) {
            active_name = s.name;
            active_rules = std::to_string(s.rule_count);
            active_flat = (s.rule_count == 0);
            break;
        } else if (active_name == "?") {
            active_name = s.name;
            active_rules = std::to_string(s.rule_count);
            active_flat = (s.rule_count == 0);
        }
    }

    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(tr("Active Keymap & Anti-Shake"));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    widgets::help_marker(tr("Shows which keymap is active and whether it is flat "
                         "(0 rules = immune to auto-switch). If Switch Rules > 0, "
                         "the layout can auto-switch mid-match."));

    ImGui::SameLine(0, 10.0f);
    if (active_flat) {
        widgets::status_pill(tr("ANTI-SHAKE STABLE"), col(theme::kOk), col(theme::kOkBg));
    } else {
        widgets::status_pill(tr("AUTO-SWITCH ACTIVE"), col(theme::kWarn), col(theme::kWarnBg));
    }

    ImGui::Dummy(ImVec2(0, 6));

    widgets::kv_row(tr("Active Mode"), lm == "145" ? tr("145 (Multiplayer / General)") : lm.c_str());
    widgets::kv_row(tr("Layout Name"), active_name);

    const ImVec4 rule_col = active_flat ? col(theme::kOk) : col(theme::kWarn);
    const std::string rules_label =
        active_rules + (active_flat
                            ? std::string(" (") + tr("0 rules - immune to layout jumping") + ")"
                            : std::string(" ") + tr("rules (can auto-switch)"));
    widgets::kv_row(tr("Switch Rules"), rules_label, &rule_col);

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void card_preflight(const App::Dashboard& d) {
    if (!d.preflight.has_value()) return;
    const auto& pf = *d.preflight;

    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));

    ImGui::BeginChild("##card_preflight", ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(tr("Install Health"));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    widgets::help_marker(tr("Verifies that this GameLoop install still matches the "
                         "schema the fixes were built against. Apply All refuses to "
                         "run when this fails, instead of silently doing nothing."));

    ImGui::SameLine(0, 10.0f);
    if (pf.overall == glt::preflight::Status::Ok) {
        widgets::status_pill(tr("HEALTHY"), col(theme::kOk), col(theme::kOkBg));
    } else if (pf.overall == glt::preflight::Status::Warn) {
        widgets::status_pill(tr("WARNINGS"), col(theme::kWarn), col(theme::kWarnBg));
    } else {
        widgets::status_pill(tr("INCOMPATIBLE"), col(theme::kError), col(theme::kErrorBg));
    }

    ImGui::Dummy(ImVec2(0, 6));

    if (pf.overall == glt::preflight::Status::Ok) {
        widgets::body_text(tr("All preflight checks passed - the install matches the schema the fixes expect."),
                           avail_w - 32.0f, col(theme::kTextDim));
    } else {
        // Detail lines are technical and stay English: they are the same lines
        // the diagnostic report carries, so a user can paste either one.
        int shown = 0;
        for (const auto& c : pf.checks) {
            if (c.status == glt::preflight::Status::Ok) continue;
            widgets::body_text(c.name + (c.detail.empty() ? "" : ": " + c.detail),
                               avail_w - 32.0f,
                               c.status == glt::preflight::Status::Fail
                                   ? col(theme::kError)
                                   : col(theme::kWarn));
            if (++shown >= 6) break;
        }
        if (shown < static_cast<int>(pf.checks.size())) {
            widgets::kv_row_continued(tr("See the diagnostic report for the full list."));
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace

void draw_dashboard(App& app) {
    theme::push_transparent();
    ImGui::BeginChild("##dash_page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##dash_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    theme::pop_transparent();

    // ── 1. Page Header with Action Buttons ─────────────────────────────────
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushFont(fonts::current().heading);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(tr("Dashboard Overview"));
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
    ImGui::TextUnformatted(tr("Live GameLoop telemetry, hardware virtualization status, and keymap integrity."));
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    // Top Right action buttons
    const float btn_w = 140.0f;
    const float actions_w = btn_w * 2.0f + 10.0f;
    if (avail_w > 640.0f) {
        ImGui::SameLine(avail_w - actions_w);
        ImGui::BeginGroup();

        ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgActive));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);

        const std::string rediag_label = std::string("\xE2\x86\xBB ") + tr("Re-diagnose");
        if (ImGui::Button(rediag_label.c_str(), ImVec2(btn_w, 30.0f))) {
            app.refresh_dashboard();
            app.refresh_host();
        }

        ImGui::SameLine(0, 10.0f);

        const std::string copylog_label = std::string("\uE8C8 ") + tr("Copy Log");
        if (ImGui::Button(copylog_label.c_str(), ImVec2(btn_w, 30.0f))) {
            std::string blob;
            for (const auto& e : app.log_entries()) {
                blob += e.timestamp + "  " + e.text + "\n";
            }
            const std::wstring wide = str::to_wide(blob);
            if (!wide.empty() && ::OpenClipboard(::GetForegroundWindow()) && ::EmptyClipboard()) {
                const HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
                if (mem) {
                    auto* dst = static_cast<wchar_t*>(::GlobalLock(mem));
                    if (dst) {
                        std::memcpy(dst, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
                        ::GlobalUnlock(mem);
                        ::SetClipboardData(CF_UNICODETEXT, mem);
                    } else {
                        ::GlobalFree(mem);
                    }
                }
                ::CloseClipboard();
                app.log("Activity log copied to clipboard.", log::Level::Success);
            }
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::EndGroup();
    }

    ImGui::Dummy(ImVec2(0, 10));

    // ── 2. InfoBar Banner ──────────────────────────────────────────────────
    const auto& host = app.host();
    const auto& alloc = app.dashboard().alloc;
    const std::string cur_m_str = alloc.memory_gb ? (std::to_string(*alloc.memory_gb) + " GB RAM") : "unset";
    const std::string cur_c_str = alloc.cpu_cores ? (std::to_string(*alloc.cpu_cores) + " Cores") : "unset";

    char info_msg[256];
    std::snprintf(info_msg, sizeof(info_msg),
                  tr("Hardware Adaptability: Host has %d Cores & %d GB RAM. Active VM allocation: %s, %s."),
                  host.cores, host.mem_gb, cur_m_str.c_str(), cur_c_str.c_str());

    widgets::infobar("\uE946", tr("System Telemetry:"), info_msg);

    ImGui::Dummy(ImVec2(0, 8));

    // ── Wi-Fi stability banner ─────────────────────────────────────────────
    if (glt::core::netinfo::is_wifi()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kWarnBg));
        ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kWarn));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
        ImGui::BeginChild("##wifi_banner_dash", ImVec2(avail_w, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::PushFont(fonts::current().body);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kWarn));
        const std::string wifi_label =
            std::string("\xE2\x9A\xA0 ") + tr("You are on Wi-Fi. Wired Ethernet gives more "
                                   "stable latency and lower jitter. See Network & Servers.");
        ImGui::TextUnformatted(wifi_label.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::Dummy(ImVec2(0, 8));
    }

    ImGui::Dummy(ImVec2(0, 4));

    // ── 3. Windows 11 Status Cards ─────────────────────────────────────────
    card_gameloop(app, app.dashboard());
    ImGui::Dummy(ImVec2(0, 8));

    card_allocation(app.dashboard(), app);
    ImGui::Dummy(ImVec2(0, 8));

    card_keymap(app.dashboard());
    ImGui::Dummy(ImVec2(0, 8));

    card_codm_settings(app.dashboard());
    ImGui::Dummy(ImVec2(0, 8));

    card_preflight(app.dashboard());
    ImGui::Dummy(ImVec2(0, 16));

    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace glt::ui