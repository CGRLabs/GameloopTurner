// glt/ui/variant_picker.cpp — first-launch variant selector.
//
// WHY THIS EXISTS
// ---------------
// The tuner ships as two builds (Beta + Hub). v1 never auto-detects which
// GameLoop the user has, so the very first launch asks, once. The answer is
// persisted (glt::variant) and the question is not asked again unless the user
// clears it from the About page.
//
// The selector records the user's GameLoop, it does NOT switch code paths: the
// compiled build governs behaviour. When the two disagree the picker says so,
// plainly, rather than silently tuning the wrong build.
#include "glt/ui/variant_picker.hpp"

#include "glt/core/variant.hpp"
#include "glt/ui/app.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"

namespace glt::ui {
namespace {

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

// One choice card. Returns true when clicked.
bool choice_card(const char* id, const char* title, const char* body, bool recommended,
                 bool is_compiled) {
    ImGui::PushID(id);

    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kBgCard));
    ImGui::PushStyleColor(ImGuiCol_Border, recommended ? col(theme::kAccent) : col(theme::kBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, recommended ? 2.0f : 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));

    ImGui::BeginChild("##card", ImVec2(avail, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    ImGui::PushFont(fonts::current().body);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    if (recommended) {
        ImGui::SameLine(0, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kAccent));
        ImGui::TextUnformatted("DEFAULT");
        ImGui::PopStyleColor();
    }
    if (is_compiled) {
        ImGui::SameLine(0, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
        ImGui::TextUnformatted("(this build)");
        ImGui::PopStyleColor();
    }

    ImGui::PushFont(fonts::current().small_text);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
    ImGui::TextWrapped("%s", body);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 6));

    const bool clicked = ImGui::Button("Select", ImVec2(120.0f, 30.0f)) != 0;

    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImGui::PopID();

    return clicked;
}

} // namespace

void draw_variant_picker(App& app) {
    if (!app.variant_picker_open()) return;

    ImGui::OpenPopup("##variant_picker");

    // The picker cannot be dismissed without answering: the whole point is that
    // the user tells us which GameLoop they have exactly once.
    if (ImGui::BeginPopupModal("##variant_picker", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushFont(fonts::current().heading);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
        ImGui::TextUnformatted("Which GameLoop do you have?");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::PushFont(fonts::current().small_text);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
        ImGui::TextWrapped(
            "The tuner ships as two builds. Pick the GameLoop you actually have — the answer "
            "is remembered and never asked again. You can change it later from the About page.");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, 8));

        const variant::Id compiled = variant::compiled();

        if (choice_card("beta", variant::label(variant::Id::Beta),
                        "The original validated target. Install root from the Program Files "
                        "scan, full performance-flag set, AppMarket.exe accepted.",
                        /*recommended=*/true,
                        /*is_compiled=*/compiled == variant::Id::Beta)) {
            app.choose_variant(variant::Id::Beta);
        }

        ImGui::Dummy(ImVec2(0, 6));

        if (choice_card("hub", variant::label(variant::Id::Hub),
                        "The multi-app Arena / TAC build (e.g. C:\\ARENA\\GameLoop). Install "
                        "root from HKLM InstallPath, new GLABox/AceKeeper processes, no "
                        "low_latency_mode or frame-gen flags.",
                        /*recommended=*/false,
                        /*is_compiled=*/compiled == variant::Id::Hub)) {
            app.choose_variant(variant::Id::Hub);
        }

        ImGui::Dummy(ImVec2(0, 8));

        ImGui::PushFont(fonts::current().small_text);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
        ImGui::TextWrapped("This binary is the %s build.", variant::label(compiled));
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::EndPopup();
    }
}

} // namespace glt::ui