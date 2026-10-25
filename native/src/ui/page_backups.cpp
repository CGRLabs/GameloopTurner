// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_backups.cpp — the Backups page.
//
// Ported from pages.py::BackupPage. Newest first, which is the reverse of the
// ascending list_backups() returns — the Python build did the same reversal,
// because "most recent backup" is the one a user reaching for Undo wants.
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/pages.hpp"

#include "glt/core/db.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/strings.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"

#include <algorithm>
#include <cstdio>

namespace glt::ui {
namespace {

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

void backup_row(App& app, const backups::BackupEntry& b, std::size_t index) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float h = 34.0f;

    // The index is the ID: it is stable for the lifetime of the row, which a
    // timestamp or tick count would not be.
    ImGui::PushID(static_cast<int>(index));
    ImGui::InvisibleButton("##row", ImVec2(avail.x, h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) {
        dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + h), theme::u32(col(theme::kBgHover)), 6.0f);
    }

    ImGui::SetCursorScreenPos(
        ImVec2(pos.x + 10.0f, pos.y + (h - ImGui::GetTextLineHeight()) * 0.5f));

    ImGui::PushFont(fonts::current().mono_small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(str::to_utf8(b.filename).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    char size[32];
    std::snprintf(size, sizeof(size), "%llu KB",
                  static_cast<unsigned long long>(b.bytes / 1024));

    const float size_w = 64.0f;
    const float btn_w = 78.0f;

    ImGui::SameLine(avail.x - size_w - btn_w - 14.0f);
    ImGui::PushFont(fonts::current().mono_small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
    ImGui::TextUnformatted(size);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::SameLine(avail.x - btn_w);
    ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kErrorBg));
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kError));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    if (ImGui::SmallButton("Restore")) app.restore_backup(index);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));
}

} // namespace

void draw_backups(App& app) {
    theme::push_transparent();
    ImGui::BeginChild("##bak_page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##bak_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    theme::pop_transparent();

    widgets::page_header("Backups",
                         "Timestamped Configs.db and smk.conf backups. Restore to undo "
                         "changes.");


    // ── Actions ────────────────────────────────────────────────────────────
    // action_row returns 0 / 1 / -1. Comparing the result with `== 0` and
    // falling through to `else` would treat "nothing pressed" (-1) as a click.
    const int action = widgets::action_row("Backup Now", "Refresh", !app.busy());

    if (action == 0) {
        app.ask("Take a fresh backup of Configs.db now?",
                [app_ptr = &app](bool yes) {
                    if (!yes) return;
                    app_ptr->log_group("Backup");
                    // Runs through the same online-backup path the fixes use, so
                    // the snapshot is consistent even with GameLoop running.
                    const std::wstring dbp = paths::appdata_db();
                    if (dbp.empty()) {
                        app_ptr->log("Configs.db not found.", log::Level::Error);
                        return;
                    }
                    const std::wstring dst = db::backup(dbp);
                    if (dst.empty()) {
                        app_ptr->log("Backup failed.", log::Level::Error);
                    } else {
                        app_ptr->log("Backup created: " + str::to_utf8(dst),
                                     log::Level::Success);
                    }
                    app_ptr->refresh_backups();
                });
    } else if (action == 1) {
        app.refresh_backups();
    }

    ImGui::Dummy(ImVec2(0, 10));

    // ── List ───────────────────────────────────────────────────────────────
    theme::begin_card(nullptr);

    const auto& list = app.backups();
    if (list.empty()) {
        widgets::body_text("No backups found. Apply a fix, or use Backup Now above.",
                           ImGui::GetContentRegionAvail().x, col(theme::kTextDim));
    } else {
        // Newest first.
        for (auto it = list.rbegin(); it != list.rend(); ++it) {
            const auto index = static_cast<std::size_t>(
                std::distance(it.base(), list.begin()));
            backup_row(app, *it, index);
        }
    }

    theme::end_card();

    ImGui::Dummy(ImVec2(0, 6));
    static bool show_raw_bak = false;
    ImGui::Checkbox("Show raw paths", &show_raw_bak);
    if (show_raw_bak && !app.backups().empty()) {
        theme::begin_card("Raw backup paths");
        ImGui::PushFont(fonts::current().small_text);
        for (const auto& b : app.backups()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
            ImGui::Text("%s  %llu B  (%s)",
                        str::to_utf8(b.filename).c_str(),
                        static_cast<unsigned long long>(b.bytes),
                        b.kind.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopFont();
        theme::end_card();
    }

    ImGui::Dummy(ImVec2(0, 12));
    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace glt::ui