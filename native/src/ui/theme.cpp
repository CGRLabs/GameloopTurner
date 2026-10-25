#include "glt/ui/theme.hpp"

#include "glt/core/log.hpp"

namespace glt::ui::theme {
namespace {

constexpr ImVec4 c(Rgb v) { return ImVec4(v.r, v.g, v.b, v.a); }

} // namespace

void apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    // ── Palette ────────────────────────────────────────────────────────────
    s.Colors[ImGuiCol_Text]                  = c(kTextPrimary);
    s.Colors[ImGuiCol_TextDisabled]         = c(kTextDim);
    s.Colors[ImGuiCol_WindowBg]             = c(kBgPrimary);
    s.Colors[ImGuiCol_ChildBg]             = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_PopupBg]             = c(kBgCard);
    s.Colors[ImGuiCol_Border]              = c(kBorder);
    s.Colors[ImGuiCol_BorderShadow]       = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_FrameBg]            = c(kInput);
    s.Colors[ImGuiCol_FrameBgHovered]     = c(kBgHover);
    s.Colors[ImGuiCol_FrameBgActive]     = c(kBgActive);
    s.Colors[ImGuiCol_TitleBg]           = c(kBgPrimary);
    s.Colors[ImGuiCol_TitleBgActive]     = c(kBgCard);
    s.Colors[ImGuiCol_TitleBgCollapsed] = c(kBgPrimary);
    s.Colors[ImGuiCol_ScrollbarBg]       = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_ScrollbarGrab]     = c(kBorder);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = c(kTextMuted);
    s.Colors[ImGuiCol_ScrollbarGrabActive] = c(kTextMuted);
    s.Colors[ImGuiCol_CheckMark]        = c(kAccent);
    s.Colors[ImGuiCol_SliderGrab]       = c(kAccent);
    s.Colors[ImGuiCol_SliderGrabActive] = c(kAccentHover);
    s.Colors[ImGuiCol_Button]          = c(kBgHover);
    s.Colors[ImGuiCol_ButtonHovered]   = c(kBgActive);
    s.Colors[ImGuiCol_ButtonActive]   = c(kBorder);
    s.Colors[ImGuiCol_Header]         = ImVec4(kAccentDim.r, kAccentDim.g, kAccentDim.b,
                                                         kAccentDim.a);
    s.Colors[ImGuiCol_HeaderHovered]  = ImVec4(0.38f, 0.27f, 0.02f, 0.55f);
    s.Colors[ImGuiCol_HeaderActive]   = ImVec4(0.45f, 0.32f, 0.02f, 0.70f);
    s.Colors[ImGuiCol_Separator]     = c(kBorder);
    s.Colors[ImGuiCol_SeparatorHovered] = c(kAccent);
    s.Colors[ImGuiCol_ResizeGrip]    = ImVec4(kAccentDim.r, kAccentDim.g, kAccentDim.b,
                                                         0.40f);
    s.Colors[ImGuiCol_Tab]           = c(kBgCard);
    s.Colors[ImGuiCol_TabHovered]   = c(kBgHover);
    s.Colors[ImGuiCol_TabActive]    = c(kBgActive);
    s.Colors[ImGuiCol_TabUnfocused] = c(kBgCard);
    s.Colors[ImGuiCol_TabUnfocusedActive] = c(kBgActive);
    s.Colors[ImGuiCol_PlotLines]   = c(kAccent);
    s.Colors[ImGuiCol_PlotLinesHovered] = c(kAccentHover);
    s.Colors[ImGuiCol_PlotHistogram] = c(kAccent);
    s.Colors[ImGuiCol_PlotHistogramHovered] = c(kAccentHover);
    s.Colors[ImGuiCol_TableHeaderBg] = c(kBgHover);
    s.Colors[ImGuiCol_TableBorderStrong] = c(kBorder);
    s.Colors[ImGuiCol_TableBorderLight] = c(kBorder);
    s.Colors[ImGuiCol_TableRowBg]   = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.025f);
    s.Colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccentDim.r, kAccentDim.g, kAccentDim.b,
                                                         0.35f);
    s.Colors[ImGuiCol_DragDropTarget] = c(kAccent);
    s.Colors[ImGuiCol_NavCursor]    = c(kAccent);
    s.Colors[ImGuiCol_NavWindowingHighlight] = c(kAccent);
    s.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);

    // ── Metrics ────────────────────────────────────────────────────────────
    // The style is tuned for this app's information-dense pages: roomy cards,
    // generous item spacing, and a scrollbar the user can actually see.
    s.WindowRounding    = 10.0f;
    s.ChildRounding     = kCardRadius;
    s.FrameRounding     = 8.0f;
    s.PopupRounding     = 10.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding      = 8.0f;
    s.TabRounding       = 8.0f;

    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 14.0f;

    s.WindowPadding     = ImVec2(kPagePadX, kPagePadY);
    s.FramePadding      = ImVec2(10.0f, 8.0f);
    s.ItemSpacing       = ImVec2(10.0f, kRowGap);
    s.ItemInnerSpacing  = ImVec2(7.0f, 5.0f);
    s.CellPadding       = ImVec2(8.0f, 5.0f);
    s.TouchExtraPadding = ImVec2(3.0f, 3.0f);

    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);

    s.Alpha                 = 1.0f;
    s.AntiAliasedLines      = true;
    s.AntiAliasedLinesUseTex = true;
    s.CircleTessellationMaxError = 0.18f;
}

ImVec4 level_color(log::Level level) {
    // Matches the log_console.py semantic: the icon and the text share a colour.
    switch (level) {
        case log::Level::Success:  return c(kOk);
        case log::Level::Error:    return c(kError);
        case log::Level::Warning:  return c(kWarn);
        case log::Level::Drift:    return c(kWarn);
        case log::Level::Progress: return c(kTextMuted);
        case log::Level::Info:
        default:                   return c(kInfo);
    }
}

void push_transparent() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
}

void pop_transparent() {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

void begin_card(const char* title, const ImVec4* dot_color, bool hero_border) {
    // A card is a borderless child window with an explicit rounding/border, so
    // ImGui's own ChildBg (fully transparent) lets the rounded corners actually
    // show against the page background.
    //
    // AutoResizeY is REQUIRED, not cosmetic. A BeginChild with size.y == 0 takes
    // the parent's REMAINING height (imgui.cpp BeginChildEx: size_default.y is
    // size_avail.y unless AutoResizeY is set). On the dashboard a card's parent
    // is an AutoResizeY wrapper inside a table cell, and such a window has no
    // height yet while its contents are still being submitted — so the card
    // measures its available height as ~0 and collapses to a two-pixel sliver.
    // Cards that sit inside a plain scroll child are unaffected, which is why
    // the failure only ever showed up on one page.
    //
    // The rest of this function is pure chrome: the palette is pushed onto the
    // current window and popped by end_card().
    const ImVec4 border = hero_border ? c(kBorderHero) : c(kBorder);
    const ImVec4 bg = c(kBgCard);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kCardRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, hero_border ? 2.0f : 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(kCardPad, kCardPad));

    ImGui::BeginChild("##card", ImVec2(0, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

    if (title && *title) {
        if (dot_color) {
            ImGui::TextColored(*dot_color, "\xE2\x97\x8F"); // ●
            ImGui::SameLine();
        }
        // The caller pushes the title font; this only sets the colour.
        ImGui::PushStyleColor(ImGuiCol_Text, c(kTextPrimary));
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
    }
}

void end_card() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace glt::ui::theme
