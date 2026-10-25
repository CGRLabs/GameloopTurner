// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/fonts.hpp — DPI-aware font atlas.
//
// WHY NOT THE IMGUI DEFAULT FONT
// ------------------------------
// Dear ImGui's built-in font is ProggyClean, a ~6 KB bitmap at one fixed size.
// It cannot be scaled (blurry when stretched) and it has almost no glyph
// coverage — no CJK, and none of the symbols this UI uses. The Python build
// used Segoe UI at 12/14/15/22/24 px and Consolas at 13 px, so the native build
// loads the same real faces from C:\Windows\Fonts and rasterises them per DPI.
//
// ICON GLYPHS
// -----------
// sidebar.py used ⬡ ⌨ ⚡ ⚙ 💾 ≡ ℹ and log_console.py used ✓ ✗ ⚠ ● ◌ ⇄.
// Segoe UI does NOT contain several of those — they would render as tofu boxes,
// which reads as a broken build. Segoe UI Symbol and Segoe MDL2 Assets are
// merged into the atlas to cover them.
//
// DPI CHANGES
// -----------
// The atlas is rebuilt when the DPI bucket changes, which happens when the
// window is dragged between a 1080p and a 4K monitor. Rebuilding invalidates
// the backend's font texture, so rebuild() reports whether it actually swapped
// the atlas and lets the caller destroy/recreate that texture — the font
// module must not call backend functions itself.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <imgui.h>

#include <string>
#include <vector>

namespace glt::ui::fonts {

// DPI buckets, matching ImGui_ImplWin32_EnableDpiAwareness() plus a quantised
// scale, so a 0.5 % DPI wobble does not rebuild a multi-megabyte atlas.
inline constexpr int kMinScale = 100;
inline constexpr int kMaxScale = 400;
inline constexpr int kScaleStep = 25;

// ── Glyph coverage ───────────────────────────────────────────────────────────
//
// Every glyph the UI can draw, as a NUL-terminated ImWchar range list.
//
// THE TRAP THIS TABLE EXISTS TO AVOID
// -----------------------------------
// AddFontFromFileTTF()'s glyph_ranges argument REPLACES ImGui's default set —
// it does not extend it. ImFontAtlas::GetGlyphRangesDefault() returns exactly
// { 0x0020, 0x00FF }, i.e. Basic Latin + Latin Supplement. So a custom table
// that carries only the decorative blocks and forgets ASCII rasterises a font
// with no letters, digits, spaces or punctuation in it. ImGui has no stated
// policy for a missing glyph here: FallbackGlyph is the '?' character, which
// is itself ASCII, so it is NULL too, and ImFont::RenderText() drops the
// character on the floor (`if (glyph == NULL) continue;` — imgui_draw.cpp).
// The result is NOT tofu boxes. It is an app whose window, cards and buttons
// all draw perfectly and whose entire text layer is invisible.
//
// 0x0020–0x00FF MUST therefore stay first in this list, and it must stay in
// the list at all. The unit tests assert it; see native/tests/test_fonts.cpp.
//
// The rest is the symbol coverage the Python build had, again block-aligned
// rather than glyph-picked, because ImGui rasterises in whole ranges:
//
//   0x0020-0x00FF  basic latin          — every label, number and separator
//   0x2000-0x206F  general punctuation
//   0x2100-0x214F  letterlike           — ℹ (sidebar: About)
//   0x2190-0x21FF  arrows               — ⇄ (drift)
//   0x2200-0x22FF  math operators       — ≡
//   0x2300-0x23FF  misc technical       — ⌘ (sidebar: Keymaps)
//   0x2460-0x24FF  enclosed alphanumerics
//   0x2500-0x257F  box drawing          — ─ (log group separator)
//   0x25A0-0x25FF  geometric shapes     — ● ▪ ▾ ▸
//   0x2600-0x26FF  misc symbols         — ✓ ✗ ⚠ ★ ⚙ ⚡
//   0x2700-0x27BF  dingbats
//   0x2900-0x297F  supplemental arrows-B — the sidebar's first nav glyph
//   0x2B00-0x2BFF  misc symbols & arrows — ⬡
//   0xE000-0xF8FF  private use          — Segoe MDL2 Assets icons
//
// Deliberately NOT ImGui's own CJK ranges: pulling a CJK atlas in would add
// tens of megabytes of texture for text this app never renders.
//
// Declared as an explicit ImWchar array rather than a string literal: ImWchar
// is `unsigned short`, a different type from `wchar_t` even on MSVC, so an
// L"..." literal needs a cast; and a narrow "\x2000" is a different value
// entirely, which /W4 rejects outright as "escape sequence out of range".
inline constexpr ImWchar kGlyphRanges[] = {
    0x0020, 0x00FF,  // Basic Latin + Latin Supplement  ← do not remove
    0x2000, 0x206F,  // General Punctuation
    0x2100, 0x214F,  // Letterlike Symbols
    0x2190, 0x21FF,  // Arrows
    0x2200, 0x22FF,  // Mathematical Operators
    0x2300, 0x23FF,  // Miscellaneous Technical
    0x2460, 0x24FF,  // Enclosed Alphanumerics
    0x2500, 0x257F,  // Box Drawing
    0x25A0, 0x25FF,  // Geometric Shapes
    0x2600, 0x26FF,  // Miscellaneous Symbols
    0x2700, 0x27BF,  // Dingbats
    0x2900, 0x297F,  // Supplemental Arrows-B
    0x2B00, 0x2BFF,  // Miscellaneous Symbols and Arrows
    0xE000, 0xF8FF,  // Private Use Area (MDL2)
    0,
};

struct Fonts {
    ImFont* body = nullptr;       // Segoe UI 14
    ImFont* small_text = nullptr;      // Segoe UI 12
    ImFont* nav = nullptr;        // Segoe UI 15
    ImFont* nav_active = nullptr; // Segoe UI Semibold 15
    ImFont* heading = nullptr;    // Segoe UI Semibold 15
    ImFont* title = nullptr;      // Segoe UI Semibold 22
    ImFont* app_title = nullptr;  // Segoe UI Semibold 24
    ImFont* mono = nullptr;       // Consolas 13
    ImFont* mono_small = nullptr; // Consolas 12

    [[nodiscard]] bool valid() const { return body != nullptr && mono != nullptr; }
};

enum class Rebuild {
    Unchanged, // same DPI bucket as last time; atlas untouched
    Ok,        // atlas swapped; caller MUST recreate the font texture
    Fallback,  // no usable system face; atlas holds the ImGui default
};

// Rebuilds the atlas for the given DPI scale (100 == 96 dpi). Safe to call
// every frame.
[[nodiscard]] Rebuild rebuild(int dpi_scale);

// The currently-loaded atlas. Valid after the first rebuild().
[[nodiscard]] const Fonts& current();

// Pushes body() and returns it. Convenience for widgets that just want "the
// normal font". Falls back to ImGui::GetFont() when the atlas is empty.
ImFont* body();

// Human-readable notes about fonts that could not be loaded, so a user with a
// stripped Windows image is told WHY the text looks wrong instead of guessing.
[[nodiscard]] const std::vector<std::string>& diagnostics();

} // namespace glt::ui::fonts