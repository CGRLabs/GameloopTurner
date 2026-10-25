// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/theme.hpp — the modern dark palette.
//
// This file is the single place the app's look is defined. Everything the UI
// draws reads its colours and metrics from the k* constants below, so a visual
// refresh means restyling here and almost nowhere else.
//
// The palette is deliberately DARK — the app's identity is a dark UI on a
// desktop — but every value has been re-picked for a modern dark look:
//
//   background  #0E1016  app chrome, sidebar
//   surface     #181C26  cards, panels
//   surface-2   #232838  hover, inputs, elevated rows
//   border      #3A404E  hairlines
//   amber       #F0A500  primary accent: Apply All, active nav, focus
//   emerald     #35C47F  "Stable" pill, success ticks
//   amber-warn  #F0B13D  drift alert
//   red         #EF5A61  failure, Revert
//   text        #E9ECF2 / #A6ABB8 / #7C8191
//
// The status colours keep their historical meanings (ok = stable, warn =
// drift, error = failure), and the amber accent keeps its historical role:
// it is the interactive colour, not a success colour.
//
// ── Raw palette ──────────────────────────────────────────────────────────────

#pragma once

#include <imgui.h>

#include <cstdint>

#include "glt/core/log.hpp"

namespace glt::ui::theme {

struct Rgb {
    float r, g, b, a;
};

// ImVec4 for the style/text APIs.
[[nodiscard]] constexpr ImVec4 vec(Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

// ImU32 for the ImDrawList primitives (AddRectFilled, AddCircleFilled, …).
//
// Those take a packed colour, not an ImVec4. Converting by hand means every
// call site needs the right cast, and one that forgets compiles as a hard
// error — which is the point. `ImVec4 -> ImU32` is not implicit in 1.91.x.
[[nodiscard]] inline ImU32 u32(Rgb c) { return ImGui::ColorConvertFloat4ToU32(vec(c)); }

[[nodiscard]] inline ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

// Surfaces.
inline constexpr Rgb kBgPrimary   {0.110f, 0.110f, 0.122f, 1.0f}; // #1C1C1F (Windows 11 dark canvas)
inline constexpr Rgb kBgSidebar   {0.094f, 0.094f, 0.106f, 1.0f}; // #18181B (Windows 11 dark sidebar)
inline constexpr Rgb kBgCard      {0.141f, 0.141f, 0.161f, 1.0f}; // #242429 (Windows 11 setting card)
inline constexpr Rgb kBgHover     {0.169f, 0.169f, 0.196f, 1.0f}; // #2B2B32 (Card hover)
inline constexpr Rgb kBgActive    {0.204f, 0.204f, 0.247f, 1.0f}; // #34343F
inline constexpr Rgb kBorder      {0.196f, 0.196f, 0.227f, 1.0f}; // #32323A (Windows 11 1px border)
inline constexpr Rgb kBorderHero  {0.961f, 0.620f, 0.043f, 0.90f}; // #F59E0B @ 90%
inline constexpr Rgb kInput       {0.118f, 0.118f, 0.137f, 1.0f}; // #1E1E23

// Accents.
inline constexpr Rgb kAccent       {0.961f, 0.620f, 0.043f, 1.0f}; // #F59E0B amber gold
inline constexpr Rgb kAccentHover  {0.984f, 0.749f, 0.141f, 1.0f}; // #FBBF24
inline constexpr Rgb kAccentDim    {0.961f, 0.620f, 0.043f, 0.22f}; // for fills
inline constexpr Rgb kOnAccent     {0.043f, 0.047f, 0.063f, 1.0f}; // near-black on amber

// Status.
inline constexpr Rgb kOk       {0.063f, 0.725f, 0.506f, 1.0f}; // #10B981 emerald
inline constexpr Rgb kWarn     {0.961f, 0.620f, 0.043f, 1.0f}; // #F59E0B
inline constexpr Rgb kError    {0.937f, 0.267f, 0.267f, 1.0f}; // #EF4444
inline constexpr Rgb kNeutral  {0.443f, 0.443f, 0.478f, 1.0f}; // #71717A
inline constexpr Rgb kInfo     {0.231f, 0.510f, 0.965f, 1.0f}; // #3B82F6

// Status pill backgrounds (low-alpha tints).
inline constexpr Rgb kOkBg     {0.063f, 0.725f, 0.506f, 0.16f};
inline constexpr Rgb kWarnBg   {0.961f, 0.620f, 0.043f, 0.16f};
inline constexpr Rgb kErrorBg  {0.937f, 0.267f, 0.267f, 0.16f};
inline constexpr Rgb kInfoBg   {0.231f, 0.510f, 0.965f, 0.16f};

// Text.
inline constexpr Rgb kTextPrimary {0.957f, 0.957f, 0.961f, 1.0f}; // #F4F4F5
inline constexpr Rgb kTextMuted   {0.631f, 0.631f, 0.667f, 1.0f}; // #A1A1AA
inline constexpr Rgb kTextDim     {0.443f, 0.443f, 0.478f, 1.0f}; // #71717A
inline constexpr Rgb kTextOnDark  {0.055f, 0.055f, 0.063f, 1.0f};

// ── Layout metrics (px, pre-DPI-scale) ──────────────────────────────────────

inline constexpr float kSidebarW   = 240.0f;
inline constexpr float kPagePadX   = 26.0f;
inline constexpr float kPagePadY   = 20.0f;
inline constexpr float kCardPad    = 14.0f;
inline constexpr float kCardRadius = 6.0f;
inline constexpr float kRowGap     = 8.0f;

// ── ImGui ────────────────────────────────────────────────────────────────────

// Applies the palette and metrics to the current style. Call once after
// ImGui::CreateContext, before the first frame.
void apply();

// Foreground colour for a log level. The log console draws the icon and the
// message text in this colour, matching log_console.py's semantic.
[[nodiscard]] ImVec4 level_color(log::Level level);

// Push/pop a transparent window background so a page can draw its own cards
// against the app chrome without a nested window's padding or border.
void push_transparent();
void pop_transparent();

// Draws a card surface: rounded, bordered, with the given header text and an
// optional status dot. Begins a child window; the caller must EndChild() via
// end_card().
//
// The caller is responsible for the title font — push it before calling.
// hero_border draws the 2 px amber ring used for the recommended fix.
void begin_card(const char* title, const ImVec4* dot_color = nullptr,
                bool hero_border = false);
void end_card();

} // namespace glt::ui::theme
