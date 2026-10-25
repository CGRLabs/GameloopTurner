// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_performance.cpp — Windows 11 Native Performance & Resource Allocation
//
// Modern Windows 11 desktop layout:
//   * Page Header with immediate action buttons (Close GameLoop, Apply)
//   * Dynamic InfoBar with host hardware detection & live VM sync
//   * Windows 11 Setting Cards with toggle switches & slider value badges
//   * Hardware-adaptive presets (Balanced, High Perf, Host Max)
//   * Empirical telemetry explanation
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/pages.hpp"

#include "glt/core/perf.hpp"
#include "glt/core/registry.hpp"
#include "glt/core/strings.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace glt::ui {
namespace {

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

int g_cores = 0;
int g_mem = 0;
bool g_initialised = false;

struct ClarityOption {
    int level;
    const char* label;
    const char* note;
};

constexpr ClarityOption kClarity[] = {
    {1, "1  Low", "Measurably LAGGIER than High on this engine - low tiers shift work to the CPU."},
    {2, "2  Clearer + FPS  \xE2\x98\x85", "The sweet spot. Clearer texture AND higher FPS. Recommended default."},
    {3, "3  High", "Raw resolution. Highest GPU cost, slower than tier 2 on this engine."},
};

bool clarity_selector(int* level) {
    bool changed = false;

    const float gap = 4.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float w = (avail - gap * 2.0f) / 3.0f;
    const float h = 32.0f;

    const ImVec2 pos0 = ImGui::GetCursorScreenPos();

    for (std::size_t i = 0; i < std::size(kClarity); ++i) {
        const bool selected = *level == kClarity[i].level;

        ImGui::PushID(static_cast<int>(kClarity[i].level));
        const ImVec2 pos(pos0.x + static_cast<float>(i) * (w + gap), pos0.y);

        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton("##seg", ImVec2(w, h));

        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        if (clicked && !selected) {
            *level = kClarity[i].level;
            changed = true;
        }

        const ImVec4 fill = selected ? col(theme::kAccent)
                                     : (hovered ? col(theme::kBgActive) : col(theme::kInput));
        const ImVec4 text = selected ? col(theme::kOnAccent) : col(theme::kTextMuted);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), theme::u32(fill), theme::kCardRadius);

        const char* label = kClarity[i].label;
        const ImVec2 ts = ImGui::CalcTextSize(label);
        ImGui::SetCursorScreenPos(
            ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushFont(fonts::current().small_text);
        ImGui::PushStyleColor(ImGuiCol_Text, text);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(ImVec2(pos0.x, pos0.y + h));
    return changed;
}

void slider_setting_row(const char* str_id, const char* icon, const char* label, const char* desc,
                        int* value, int min_val, int max_val, const char* unit, const char* badge = nullptr) {
    ImGui::PushID(str_id);

    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));

    std::string child_name = std::string("##slider_card_") + str_id;
    ImGui::BeginChild(child_name.c_str(), ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    const float content_w = ImGui::GetContentRegionAvail().x;
    const float control_w = 260.0f;
    const float text_w = std::max(100.0f, content_w - control_w - 20.0f);

    if (icon && *icon) {
        ImGui::PushFont(fonts::current().heading);
        ImGui::TextUnformatted(icon);
        ImGui::PopFont();
        ImGui::SameLine(0, 12.0f);
    }

    ImGui::BeginGroup();
    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    if (badge && *badge) {
        ImGui::SameLine(0, 8.0f);
        widgets::status_pill(badge, col(theme::kAccent), col(theme::kAccentDim));
    }

    if (desc && *desc) {
        widgets::body_text(desc, text_w, col(theme::kTextMuted));
    }
    ImGui::EndGroup();

    // Right-aligned slider + value pill
    ImGui::SameLine(std::max(0.0f, content_w - control_w));
    ImGui::BeginGroup();

    const float slider_w = 175.0f;
    ImGui::SetNextItemWidth(slider_w);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, col(theme::kAccent));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, col(theme::kAccentHover));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, col(theme::kInput));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col(theme::kBgHover));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    char slider_id[32];
    std::snprintf(slider_id, sizeof(slider_id), "##slider_%s", str_id);
    ImGui::SliderInt(slider_id, value, min_val, max_val, "");

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::SameLine(0, 10.0f);
    char val_buf[32];
    std::snprintf(val_buf, sizeof(val_buf), "%d %s", *value, unit);
    widgets::status_pill(val_buf, col(theme::kAccent), col(theme::kInput));

    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void card_allocation(App& app) {
    const auto& alloc = app.dashboard().alloc;
    const int max_c = app.max_cores();
    const int max_m = app.max_mem_gb();

    // Auto-sync with detected reality if not actively editing
    if (!app.alloc_pending()) {
        if (alloc.cpu_cores && *alloc.cpu_cores > 0) {
            g_cores = static_cast<int>(*alloc.cpu_cores);
        } else if (!g_initialised) {
            g_cores = std::min(6, max_c);
        }

        if (alloc.memory_gb && *alloc.memory_gb > 0) {
            g_mem = static_cast<int>(*alloc.memory_gb);
        } else if (!g_initialised) {
            g_mem = std::min(10, max_m);
        }
        g_initialised = true;
    }

    g_cores = std::clamp(g_cores, 2, max_c);
    g_mem = std::clamp(g_mem, 3, max_m);

    const int old_cores = g_cores;
    const int old_mem = g_mem;

    // RAM Setting Card
    std::string ram_desc = "Sets vm_memory_gb in GameLoop registry. Currently assigned: " +
                           (alloc.memory_gb ? (std::to_string(*alloc.memory_gb) + " GB") : "unset") + ".";
    slider_setting_row("ram", "\uE7F8", "VM Memory (RAM) Allocation", ram_desc.c_str(),
                       &g_mem, 3, max_m, "GB", "LIVE SYNCED");

    ImGui::Dummy(ImVec2(0, 4));

    // CPU Cores Setting Card
    std::string cores_desc = "Allocates physical execution threads for the emulated Android subsystem (leaves 1 core free for Windows). Currently: " +
                             (alloc.cpu_cores ? (std::to_string(*alloc.cpu_cores) + " Cores") : "unset") + ".";
    slider_setting_row("cores", "\uE945", "Virtual CPU Cores", cores_desc.c_str(),
                       &g_cores, 2, max_c, "Cores");

    // Queue allocation changes if modified
    if (g_cores != old_cores || g_mem != old_mem) {
        app.set_alloc_pending(g_cores, g_mem, true);
    }
}

void card_clarity(App& app) {
    int detected_clarity = 0;
    if (!app.dashboard().local_user_config.empty()) {
        try {
            const auto j = nlohmann::json::parse(app.dashboard().local_user_config);
            if (j.contains("clarity")) {
                if (j["clarity"].is_number()) {
                    detected_clarity = j["clarity"].get<int>();
                } else if (j["clarity"].is_string()) {
                    detected_clarity = std::stoi(j["clarity"].get<std::string>());
                }
            }
        } catch (...) {}
    }

    int level = app.clarity_level();
    if (!app.clarity_pending() && detected_clarity >= 1 && detected_clarity <= 3) {
        level = detected_clarity;
    }

    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));

    ImGui::BeginChild("##clarity_card", ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    const float content_w = ImGui::GetContentRegionAvail().x;
    const float control_w = 360.0f;
    const float text_w = std::max(100.0f, content_w - control_w - 20.0f);

    ImGui::PushFont(fonts::current().heading);
    ImGui::TextUnformatted("\uE8B9");
    ImGui::PopFont();
    ImGui::SameLine(0, 12.0f);

    ImGui::BeginGroup();
    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted("Render Clarity Tier");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    widgets::help_marker("Render resolution. Tier 2 = clearer textures AND higher FPS here. "
                         "Tier 1 was actually LAGGIER (shifts work to the CPU). "
                         "Tier 3 = highest GPU cost, slower than tier 2 on this engine.");

    ImGui::SameLine(0, 8.0f);
    widgets::status_pill("TIER 2 RECOMMENDED", col(theme::kAccent), col(theme::kAccentDim));

    std::string cur_tier_str = (detected_clarity >= 1 && detected_clarity <= 3)
                                   ? ("Currently in GameLoop: Tier " + std::to_string(detected_clarity))
                                   : "Currently in GameLoop: auto / unset";
    widgets::body_text(cur_tier_str, text_w, col(theme::kTextMuted));
    widgets::body_text("Tier 2 gives cleaner textures & highest FPS without shifting emulation workload to CPU threads.",
                       text_w, col(theme::kTextDim));
    ImGui::EndGroup();

    ImGui::SameLine(std::max(0.0f, content_w - control_w));
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(control_w);
    if (clarity_selector(&level)) {
        app.set_clarity_pending(level, true);
    }
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

void card_renderer() {
    const auto alloc = reg::perf_allocation();

    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));

    ImGui::BeginChild("##renderer_card", ImVec2(avail_w, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted("DirectX / Engine Renderer (Read-Only)");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    widgets::help_marker("Auto (0) is the official vendor path: GameLoop switches "
                         "between DirectX+ and OpenGL+ per game map. The tuner "
                         "preserves this to prevent shader compiler crashes.");

    const auto val = [](const reg::RegValue& v) -> std::string {
        return v ? std::to_string(*v) : std::string("unset");
    };

    widgets::kv_row("RenderMode",
                    val(alloc.render_mode) + " - " +
                        str::to_utf8(reg::render_mode_label(alloc.render_mode)));
    widgets::kv_row("RenderUserType",
                    val(alloc.render_user_type) + " - " +
                        str::to_utf8(reg::render_user_label(alloc.render_user_type)));

    widgets::body_text(
        "Auto (0) is the official vendor path: GameLoop automatically switches between DirectX+ and OpenGL+ per game map. "
        "The tuner intentionally preserves this to prevent shader compiler crashes.",
        ImGui::GetContentRegionAvail().x, col(theme::kTextDim));

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace

void draw_performance(App& app) {
    theme::push_transparent();
    ImGui::BeginChild("##perf_page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##perf_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    theme::pop_transparent();

    // ── 1. Windows 11 Page Header with Action Buttons ──────────────────────
    const float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushFont(fonts::current().heading);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted("Performance & Resource Allocation");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
    ImGui::TextUnformatted("Synchronize VM hardware limits, direct DirectX rendering, and input latency directly to GameLoop.");
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    // Action buttons docked to top right
    const int pending_n = app.pending_count();
    const float btn_h = 30.0f;
    const float btn_w = 140.0f;
    const float actions_w = btn_w * 2.0f + 10.0f;

    if (avail_w > 640.0f) {
        ImGui::SameLine(avail_w - actions_w);
        ImGui::BeginGroup();

        // Close GameLoop Button
        const bool can_close = !app.busy();
        ImGui::BeginDisabled(!can_close);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.12f, 0.12f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kError));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
        if (ImGui::Button("\xE2\x9C\x95 Close GameLoop", ImVec2(btn_w, btn_h))) {
            app.close_gameloop();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();

        ImGui::SameLine(0, 10.0f);

        // Apply Button
        const bool can_apply = pending_n > 0 && !app.busy();
        char apply_label[48];
        if (pending_n > 0) {
            std::snprintf(apply_label, sizeof(apply_label), "\xE2\x9C\x93 Apply (%d)", pending_n);
        } else {
            std::snprintf(apply_label, sizeof(apply_label), "\xE2\x9C\x93 Apply Changes");
        }

        ImGui::BeginDisabled(!can_apply);
        ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kAccent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kAccentHover));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kOnAccent));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
        if (ImGui::Button(apply_label, ImVec2(btn_w, btn_h))) {
            app.apply_all();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();

        ImGui::EndGroup();
    }

    ImGui::Dummy(ImVec2(0, 10));

    // ── 2. Windows 11 InfoBar & Presets Banner ─────────────────────────────
    const auto& host = app.host();
    const auto& alloc = app.dashboard().alloc;
    const std::string cur_m_str = alloc.memory_gb ? (std::to_string(*alloc.memory_gb) + " GB RAM") : "unset";
    const std::string cur_c_str = alloc.cpu_cores ? (std::to_string(*alloc.cpu_cores) + " Cores") : "unset";

    char info_msg[256];
    std::snprintf(info_msg, sizeof(info_msg),
                  "Host detected with %d Cores & %d GB RAM. GameLoop currently allocated: %s, %s.",
                  host.cores, host.mem_gb, cur_m_str.c_str(), cur_c_str.c_str());

    widgets::infobar("\uE946", "Hardware Adaptability:", info_msg);

    // Hardware Presets Chips
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextColored(col(theme::kTextDim), "Quick Presets:");
    ImGui::SameLine(0, 10.0f);

    const int max_c = app.max_cores();
    const int max_m = app.max_mem_gb();
    const int preset_bal_c = std::clamp(host.cores / 2, 2, max_c);
    const int preset_bal_m = std::clamp(host.mem_gb / 2, 3, max_m);
    const int preset_perf_c = std::clamp((host.cores * 3) / 4, 2, max_c);
    const int preset_perf_m = std::clamp((host.mem_gb * 3) / 4, 3, max_m);

    char buf_bal[48], buf_perf[48], buf_max[48];
    std::snprintf(buf_bal, sizeof(buf_bal), "Balanced (%dC / %dGB)", preset_bal_c, preset_bal_m);
    std::snprintf(buf_perf, sizeof(buf_perf), "High Perf \xE2\x98\x85 (%dC / %dGB)", preset_perf_c, preset_perf_m);
    std::snprintf(buf_max, sizeof(buf_max), "Host Max (%dC / %dGB)", max_c, max_m);

    const bool is_bal = (g_cores == preset_bal_c && g_mem == preset_bal_m);
    const bool is_perf = (g_cores == preset_perf_c && g_mem == preset_perf_m);
    const bool is_max = (g_cores == max_c && g_mem == max_m);

    if (widgets::chip_button(buf_bal, is_bal)) {
        g_cores = preset_bal_c;
        g_mem = preset_bal_m;
        app.set_alloc_pending(g_cores, g_mem, true);
    }
    ImGui::SameLine(0, 8.0f);
    if (widgets::chip_button(buf_perf, is_perf)) {
        g_cores = preset_perf_c;
        g_mem = preset_perf_m;
        app.set_alloc_pending(g_cores, g_mem, true);
    }
    ImGui::SameLine(0, 8.0f);
    if (widgets::chip_button(buf_max, is_max)) {
        g_cores = max_c;
        g_mem = max_m;
        app.set_alloc_pending(g_cores, g_mem, true);
    }

    ImGui::Dummy(ImVec2(0, 14));

    // ── 3. Section: Virtual Machine Resources ──────────────────────────────
    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted("Virtual Machine Resources");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    widgets::help_marker("How much of your PC's CPU and RAM GameLoop's VM gets. "
                         "Leave 1 core and 2 GB for Windows. Needs GameLoop restart.");

    ImGui::Dummy(ImVec2(0, 4));
    card_allocation(app);

    ImGui::Dummy(ImVec2(0, 6));
    card_clarity(app);

    ImGui::Dummy(ImVec2(0, 16));

    // ── 4. Section: Engine & Input Latency Flags ───────────────────────────
    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted("Engine & Input Latency Flags");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    widgets::help_marker("Low-latency: smaller frame queue = best feel-per-cost (keep ON). "
                         "Frame-gen: fake frames that add input lag (keep OFF).");

    ImGui::Dummy(ImVec2(0, 4));

    for (const auto& def : fixes::for_page(fixes::Page::Performance)) {
        bool on = app.is_pending(def.key);

        std::string badge;
        std::string icon = "\uE945"; // MDL2 LightningBolt
        if (def.key == fixes::Key::Latency) {
            badge = "LOW-LATENCY QUEUE";
            icon = "\uE823"; // MDL2 Clock
        } else if (def.key == fixes::Key::NoFrameGen) {
            badge = "RAW FPS";
            icon = "\uE714"; // MDL2 Play
        }

        std::string desc = std::string(def.why);
        if (!def.risk.empty()) { desc += "\n\nRisk: "; desc += def.risk; }
        if (widgets::setting_row(std::to_string(static_cast<int>(def.key)).c_str(),
                                 icon.c_str(), def.label, desc, &on, badge.c_str())) {
            app.toggle_pending(def.key, on);
        }

        ImGui::Dummy(ImVec2(0, 4));
    }

    ImGui::Dummy(ImVec2(0, 12));

    // ── 5. Section: DirectX Renderer (Read-Only) ───────────────────────────
    card_renderer();

    ImGui::Dummy(ImVec2(0, 8));

    // ── 5b. Show raw values (power users) ──────────────────────────────────
    static bool show_raw_perf = false;
    ImGui::Checkbox("Show raw values (DB rows, registry)", &show_raw_perf);
    if (show_raw_perf) {
        theme::begin_card("Raw performance data");
        const auto regalloc = reg::perf_allocation();
        char buf[128];
        std::snprintf(buf, sizeof(buf), "PerformanceMode=%s",
                      regalloc.performance_mode ? std::to_string(*regalloc.performance_mode).c_str() : "unset");
        widgets::kv_row("reg", buf);
        std::snprintf(buf, sizeof(buf), "PerformanceMemoryGb=%s",
                      regalloc.memory_gb ? std::to_string(*regalloc.memory_gb).c_str() : "unset");
        widgets::kv_row("reg", buf);
        std::snprintf(buf, sizeof(buf), "PerformanceCpuCoreNum=%s",
                      regalloc.cpu_cores ? std::to_string(*regalloc.cpu_cores).c_str() : "unset");
        widgets::kv_row("reg", buf);
        std::snprintf(buf, sizeof(buf), "RenderMode=%s",
                      regalloc.render_mode ? std::to_string(*regalloc.render_mode).c_str() : "unset");
        widgets::kv_row("reg", buf);
        std::snprintf(buf, sizeof(buf), "RenderUserType=%s",
                      regalloc.render_user_type ? std::to_string(*regalloc.render_user_type).c_str() : "unset");
        widgets::kv_row("reg", buf);

        if (!app.dashboard().local_user_config.empty()) {
            widgets::kv_row("local_user_config", app.dashboard().local_user_config);
        }
        if (!app.dashboard().host_config.empty()) {
            widgets::kv_row("host_config", app.dashboard().host_config);
        }
        theme::end_card();
    }

    // ── 6. Section: Why These Values ───────────────────────────────────────
    ImGui::Dummy(ImVec2(0, 10));
    theme::begin_card("Why These Values (Empirical Telemetry)");
    widgets::help_marker("Empirical notes from testing on this engine. Tier 1 was "
                         "laggier than tier 3 (shifts work to CPU). Low-latency = "
                         "best feel-per-cost. Frame-gen = fake frames add lag.");
    for (const auto& [key, text] : perf::explain_table()) {
        if (key != perf::kLowLatency && key != perf::kFrameGen && key != perf::kFrameRate) {
            continue;
        }
        widgets::kv_row(key, text);
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace glt::ui