// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/sidebar.cpp — Windows 11 Navigation Sidebar
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/sidebar.hpp"

#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace glt::ui {
namespace {

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

struct NavItem {
    Page page;
    const char* label;
    const char* glyph;
};

// Segoe MDL2 Assets glyphs (Private Use Area, already merged into the atlas).
//
// The previous entries were astral-plane emoji (U+1F300 block). They rendered
// as '?' because kGlyphRanges stops at 0x2BFF — the astral blocks were never
// rasterised, so ImGui substituted its fallback glyph. MDL2 is a monochrome
// icon font that ships with Windows, lives in 0xE000-0xF8FF (covered), and is
// what the Windows 11 look actually calls for.
constexpr NavItem kNav[] = {
    {Page::Dashboard,   "Dashboard",            "\uE80F"}, // ViewAll
    {Page::Performance, "Performance",          "\uE945"}, // LightningBolt
    {Page::Keymaps,     "Keymaps & Anti-Shake", "\uE8EC"}, // Keyboard
    {Page::VmPriority,  "VM & Priority",        "\uE713"}, // Settings
    {Page::Network,     "Network & Servers",    "\uE12B"}, // World
    {Page::Backups,     "Backups",              "\uE74E"}, // Save
    {Page::Logs,        "Diagnostics & Logs",   "\uE9D5"}, // Document
    {Page::About,       "About",                "\uE946"}, // Info
};

bool nav_row(App& app, const NavItem& item, bool active) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float h = 38.0f;

    ImGui::PushID(static_cast<int>(item.page));
    ImGui::InvisibleButton("##nav", ImVec2(avail.x, h));
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
        // Windows 11 Left accent indicator bar + card fill
        dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + h),
                          theme::u32(col(theme::kBgActive)), theme::kCardRadius);
        dl->AddRectFilled(ImVec2(pos.x + 1.0f, pos.y + 7.0f),
                          ImVec2(pos.x + 3.5f, pos.y + h - 7.0f),
                          theme::u32(col(theme::kAccent)), 2.0f);
    } else if (hovered) {
        dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + h),
                          theme::u32(col(theme::kBgHover)), theme::kCardRadius);
    }

    ImGui::SetCursorScreenPos(
        ImVec2(pos.x + 14.0f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f));

    ImGui::PushFont(active ? fonts::current().nav_active : fonts::current().nav);
    ImGui::PushStyleColor(ImGuiCol_Text,
                          active ? col(theme::kTextPrimary) : col(theme::kTextMuted));
    ImGui::TextUnformatted(item.glyph);
    ImGui::SameLine(0, 10.0f);
    ImGui::TextUnformatted(item.label);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));
    return clicked;
}

} // namespace

void draw_sidebar(App& app) {
    const float w = theme::kSidebarW;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(theme::kBgSidebar));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGui::BeginChild("##sidebar", ImVec2(w, 0), ImGuiChildFlags_Borders);

    // ── App Brand Header ───────────────────────────────────────────────────
    ImGui::PushFont(fonts::current().app_title);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted("CODM Optimizer");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::PushFont(fonts::current().small_text);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
    ImGui::TextUnformatted("GameLoop Engine 4.4 / 7.1");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    // ── Navigation ─────────────────────────────────────────────────────────
    for (const auto& item : kNav) {
        if (nav_row(app, item, app.page() == item.page)) {
            app.show_page(item.page);
        }
    }

    // ── Spacer: pushes the status stack to the bottom ──────────────────────
    const float remaining_y = ImGui::GetContentRegionAvail().y;
    if (remaining_y > 180.0f) {
        ImGui::Dummy(ImVec2(0, remaining_y - 180.0f));
    }

    // ── Status Widget Card ─────────────────────────────────────────────────
    {
        const auto& d = app.dashboard();
        const bool running = !d.procs.empty();
        const auto& alloc = d.alloc;

        const float avail_w = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
        ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kBorder));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));

        ImGui::BeginChild("##sb_status_widget", ImVec2(avail_w, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

        ImGui::PushFont(fonts::current().small_text);

        // Line 1: GameLoop status
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
        ImGui::TextUnformatted("GameLoop");
        ImGui::PopStyleColor();

        ImGui::SameLine(avail_w - 95.0f);
        if (running) {
            widgets::status_dot(col(theme::kOk), 6.0f);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kOk));
            ImGui::Text("Running (%u)", d.procs.front().pid);
            ImGui::PopStyleColor();
        } else {
            widgets::status_dot(col(theme::kNeutral), 6.0f);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
            ImGui::TextUnformatted("Not running");
            ImGui::PopStyleColor();
        }

        // Line 2: Registry sync
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
        ImGui::TextUnformatted("VM Sync");
        ImGui::PopStyleColor();

        ImGui::SameLine(avail_w - 95.0f);
        char sync_buf[48];
        const int c = alloc.cpu_cores.value_or(0);
        const int m = alloc.memory_gb.value_or(0);
        if (c > 0 || m > 0) {
            std::snprintf(sync_buf, sizeof(sync_buf), "%d GB / %d C", m, c);
        } else {
            std::snprintf(sync_buf, sizeof(sync_buf), "unset");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kAccent));
        ImGui::TextUnformatted(sync_buf);
        ImGui::PopStyleColor();

        ImGui::PopFont();
        ImGui::EndChild();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    ImGui::Dummy(ImVec2(0, 6));

    // ── Quick Launch Button ────────────────────────────────────────────────
    {
        const bool can_launch = !app.busy();
        ImGui::BeginDisabled(!can_launch);
        ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgActive));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
        if (ImGui::Button("\xE2\x96\xB6  Launch GameLoop", ImVec2(ImGui::GetContentRegionAvail().x, 28.0f))) {
            app.launch_gameloop();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }

    ImGui::Dummy(ImVec2(0, 4));

    // ── Drift Badge ────────────────────────────────────────────────────────
    {
        const DriftState state = app.drift_state();
        const ImVec4 fg = state == DriftState::Stable  ? col(theme::kOk)
                          : state == DriftState::Drifted ? col(theme::kWarn)
                                                        : col(theme::kNeutral);
        const ImVec4 bg = state == DriftState::Stable  ? col(theme::kOkBg)
                          : state == DriftState::Drifted ? col(theme::kWarnBg)
                                                        : col(theme::kBgHover);

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float h = 26.0f;
        ImGui::Dummy(ImVec2(avail.x, h));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + h), theme::u32(bg), theme::kCardRadius);

        ImGui::SetCursorScreenPos(ImVec2(pos.x + 8.0f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
        widgets::status_dot(fg, 6.0f);
        ImGui::SameLine();
        ImGui::PushFont(fonts::current().small_text);
        ImGui::PushStyleColor(ImGuiCol_Text, fg);
        ImGui::TextUnformatted(drift_text(state));
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    ImGui::Dummy(ImVec2(0, 4));

    // ── Apply / Cancel Buttons (only when changes are pending) ─────────────
    {
        const int n = app.pending_count();
        if (n > 0) {
            const std::string label = "\xE2\x9C\x93  Apply (" + std::to_string(n) + " queued)";

            const bool enabled = !app.busy();
            if (widgets::action_button(label.c_str(), enabled, col(theme::kAccent))) {
                app.apply_all();
            }

            ImGui::Dummy(ImVec2(0, 4));

            ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgActive));
            ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
            if (ImGui::Button("Cancel changes", ImVec2(ImGui::GetContentRegionAvail().x, 26.0f))) {
                app.clear_pending();
                app.log("Pending changes cleared.", log::Level::Info);
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
        } else {
            ImGui::PushFont(fonts::current().small_text);
            ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
            ImGui::TextUnformatted("No pending changes.");
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

} // namespace glt::ui