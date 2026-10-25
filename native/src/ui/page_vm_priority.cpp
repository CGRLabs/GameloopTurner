// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_vm_priority.cpp — the VM & Priority page.
//
// Ported from pages.py::FixPage for tabname='VM & Priority'.
//
// This is the one page whose fix does NOT require GameLoop to be closed:
// priorities apply to the running processes and reset when they exit. That
// distinction is called out on the page, because "close GameLoop first" would
// make this fix impossible to use.
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/pages.hpp"

#include "glt/core/procs.hpp"
#include "glt/core/strings.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"

namespace glt::ui {
namespace {

void fix_row(App& app, const fixes::Def& def) {
    bool on = app.is_pending(def.key);
    ImGui::PushID(static_cast<int>(def.key));
    widgets::fix_checkbox(&on, def.label, def.why);
    if (on != app.is_pending(def.key)) app.toggle_pending(def.key, on);
    ImGui::PopID();
}

} // namespace

void draw_vm_priority(App& app) {
    theme::push_transparent();
    ImGui::BeginChild("##vmp_page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##vmp_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    theme::pop_transparent();

    widgets::page_header(
        "VM & Priority",
        "Priorities apply to RUNNING processes - launch the game first, then Apply All. "
        "They reset when GameLoop exits.");

    for (const auto& def : fixes::for_page(fixes::Page::VmPriority)) {
        bool on = app.is_pending(def.key);
        std::string desc = std::string(def.why);
        if (!def.risk.empty()) { desc += "\n\nRisk: "; desc += def.risk; }
        if (widgets::setting_row(std::to_string(static_cast<int>(def.key)).c_str(),
                                 "\xE2\x9A\x99", def.label, desc, &on, "REAL-TIME")) {
            app.toggle_pending(def.key, on);
        }
        ImGui::Dummy(ImVec2(0, 4));
    }

    // Live process table, so the user can see whether the target is even
    // running before ticking the box.
    ImGui::Dummy(ImVec2(0, 10));
    theme::begin_card("Running GameLoop processes");
    widgets::help_marker("Live list of running GameLoop processes with PID and "
                         "memory. Use this to confirm the game is running before "
                         "applying priorities.");

    const auto& procs = app.dashboard().procs;
    if (procs.empty()) {
        widgets::body_text("Nothing running. Start GameLoop, then re-diagnose from the "
                           "Dashboard and tick the fix above.",
                           ImGui::GetContentRegionAvail().x,
                           ImVec4(theme::kTextDim.r, theme::kTextDim.g, theme::kTextDim.b, 1.0f));
    } else {
        const ImGuiID table_id = ImGui::GetID("##proc_table");
        if (ImGui::BeginTable("##proc_table", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Process", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Memory", ImGuiTableColumnFlags_WidthFixed, 100.0f);

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (const auto& p : procs) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(str::to_utf8(p.name).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", p.pid);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%llu MB", static_cast<unsigned long long>(p.mem_kb / 1024));
            }
            ImGui::EndTable();
        }
        (void)table_id;
    }

    theme::end_card();

    ImGui::Dummy(ImVec2(0, 12));
    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace glt::ui