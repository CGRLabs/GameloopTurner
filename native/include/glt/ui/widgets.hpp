// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/widgets.hpp — small drawing primitives shared by every page.
//
// Immediate mode has no widget tree, so the pieces that pages.py implemented as
// CustomTkinter frames are reproduced here as thin helpers over ImGui. Each one
// keeps its own ImGui ID scope so two cards on the same page never collide.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <imgui.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "glt/core/log.hpp"
#include "glt/ui/fonts.hpp"
#include "glt/ui/theme.hpp"

namespace glt::ui {

class App;

namespace widgets {

// A (label, value) row. The label gets a fixed-width column so the values line
// up down the card, matching dashboard.py's width=130 label.
void kv_row(std::string_view label, std::string_view value,
            const ImVec4* value_color = nullptr);

// A key/value row where the label is empty — an indented continuation.
void kv_row_continued(std::string_view value, const ImVec4* value_color = nullptr);

// A labelled gauge. `fraction` is clamped to [0,1]; the bar recolours to amber
// then red as it approaches the limit.
void gauge(std::string_view label, float value, float maximum, std::string_view unit,
           float warn_below = 0.0f);

// A checkbox with a heading and a wrapped explanation underneath. This is the
// fix row from pages.py: FixPage._add_fix.
void fix_checkbox(bool* value, std::string_view label, std::string_view why);

// The amber-ringed hero variant, with a "RECOMMENDED" ribbon.
void fix_hero_checkbox(bool* value, std::string_view label, std::string_view why);

// A pill: rounded background + text. Used for the drift badge and the status
// dots in card headers.
void status_pill(std::string_view text, const ImVec4& fg, const ImVec4& bg);

// A coloured status dot, sized in logical pixels.
void status_dot(const ImVec4& color, float diameter = 8.0f);

// A small coloured square bullet, for the log console's level indicator.
void level_icon(log::Level level);

// Modern Windows 11 Toggle Switch (pill with sliding knob)
bool toggle_switch(std::string_view str_id, bool* v);

// Modern Windows 11 Setting Card row with icon, title, description, and toggle
bool setting_row(std::string_view str_id, std::string_view icon, std::string_view label,
                 std::string_view desc, bool* value, std::string_view badge = {});

// Modern Windows 11 InfoBar banner with accent border
void infobar(std::string_view icon, std::string_view title, std::string_view message,
             const ImVec4* border_col = nullptr);

// Quick hardware preset chip button
bool chip_button(std::string_view label, bool active);

// Section heading + muted explanatory line, as used at the top of each page.
void page_header(std::string_view title, std::string_view intro = {});

// A horizontal separator with 10 px of breathing room either side.
void separator();

// A small "?" help icon that shows a tooltip on hover.
void help_marker(const char* desc);

// A tooltip that appears when the previous item is hovered.
void tooltip(const char* desc);

// A disabled-when-busy button that reports whether it was pressed.
bool action_button(std::string_view label, bool enabled, const ImVec4& accent);

// Two-button row: primary (left) and secondary (right). Returns the pressed
// index, or -1.
int action_row(std::string_view primary, std::string_view secondary, bool enabled);

// Text wrapped to `width`, drawn dimmed. The "why" text under every fix.
void body_text(std::string_view text, float width, const ImVec4& color);

// Starts a collapsible region. Returns true while open. Uses a stable ID
// derived from `label`.
bool collapsible(std::string_view label, bool* open);

// ── Log console ──────────────────────────────────────────────────────────────

// Renders the app's log entries with a clipper, auto-scroll and a level dot.
// This is log_console.py as an immediate-mode list.
void log_console(App& app, float height);

} // namespace widgets
} // namespace glt::ui