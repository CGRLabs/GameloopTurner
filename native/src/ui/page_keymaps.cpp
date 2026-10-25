// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_keymaps.cpp — the Keymaps page.
//
// Ported from pages.py::FixPage for tabname='Keymaps': the starred fullchain
// fix renders as an amber-ringed hero card, and everything else collapses into
// an "Advanced options" expander, which is the exact split the Python build
// used. The reason for that split is worth keeping: fullchain is the only fix a
// user should need, and eight checkboxes side by side actively discourage
// finding it.
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/pages.hpp"

#include "glt/core/strings.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"

namespace glt::ui {
namespace {

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

// Expander state. There is exactly one App per process, so a function-local is
// the right lifetime — and it is far clearer than round-tripping the value
// through ImGui's internal storage.
bool g_advanced_open = false;

void fix_row(App& app, const fixes::Def& def, bool hero) {
    bool on = app.is_pending(def.key);
    const char* badge = hero ? "\xE2\x98\x85 RECOMMENDED" : nullptr;
    const char* icon = hero ? "\uE8EC" : "\uE713";

    std::string desc = std::string(def.why);
    if (!def.risk.empty()) {
        desc += "\n\nRisk: ";
        desc += def.risk;
    }

    if (widgets::setting_row(std::to_string(static_cast<int>(def.key)).c_str(),
                             icon, def.label, desc, &on, badge)) {
        app.toggle_pending(def.key, on);
    }
}

} // namespace

void draw_keymaps(App& app) {
    theme::push_transparent();
    ImGui::BeginChild("##km_page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##km_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    theme::pop_transparent();

    widgets::page_header(
        "Keymaps & Anti-Shake",
        "Prevent emulator layout auto-switching and mouse jitter during gunfights.");

    const auto defs = fixes::for_page(fixes::Page::Keymaps);

    // Hero first.
    for (const auto& def : defs) {
        if (def.star) {
            fix_row(app, def, true);
            ImGui::Dummy(ImVec2(0, 4));
        }
    }

    // The rest, behind an expander.
    if (widgets::collapsible("Advanced Keymap Options", &g_advanced_open)) {
        for (const auto& def : defs) {
            if (def.star) continue;
            fix_row(app, def, false);
            ImGui::Dummy(ImVec2(0, 4));
        }
    }

    ImGui::Dummy(ImVec2(0, 12));
    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace glt::ui