// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_logs.cpp — the activity log.
//
// Ported from legacy-python/src/ui/log_console.py, plus the header bar from
// pages.py::LogPage. Group headers, per-level glyphs, timestamps, auto-scroll.
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/pages.hpp"

#include "glt/core/log.hpp"
#include "glt/core/strings.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"

namespace glt::ui {
namespace {

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

} // namespace

void draw_logs(App& app) {
    theme::push_transparent();
    ImGui::BeginChild("##log_page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    theme::pop_transparent();

    widgets::page_header("Activity Log",
                         "Every operation from all pages, with secrets stripped.");

    static bool show_raw_logs = false;
    ImGui::Checkbox("Show raw (log file path, entry count)", &show_raw_logs);
    if (show_raw_logs) {
        theme::begin_card("Raw log info");
        widgets::kv_row("Log file", str::to_utf8(glt::log::file_path()));
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%zu", app.log_entries().size());
        widgets::kv_row("Entries in ring", buf);
        widgets::kv_row("Ring cap", "2000");
        theme::end_card();
        ImGui::Dummy(ImVec2(0, 6));
    }

    // ── Header bar ─────────────────────────────────────────────────────────
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(avail.x, 34.0f));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + 34.0f), theme::u32(col(theme::kBgCard)),
                          8.0f);

        // Auto-scroll toggle, right-aligned. (The local cannot be called `auto` —
        // that is the type-deduction keyword.)
        bool follow = app.log_auto_scroll();
        ImGui::SetCursorScreenPos(
            ImVec2(pos.x + avail.x - 140.0f, pos.y + (34.0f - ImGui::GetFrameHeight()) / 2));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, col(theme::kAccent));
        if (ImGui::Checkbox("Auto-scroll", &follow)) app.set_log_auto_scroll(follow);
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 10.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgActive));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
        if (ImGui::Button("Clear", ImVec2(64.0f, 0.0f))) app.clear_log();
        ImGui::PopStyleColor(3);

        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + 34.0f));
    }

    ImGui::Dummy(ImVec2(0, 8));

    // ── Console ────────────────────────────────────────────────────────────
    // Reserve the remainder of the viewport; the clipper inside handles
    // virtualisation, so a 2000-entry log costs the same to draw as a 20-entry
    // one.
    const float height = std::max(120.0f, ImGui::GetContentRegionAvail().y - 16.0f);
    widgets::log_console(app, height);

    ImGui::EndChild();
}

} // namespace glt::ui