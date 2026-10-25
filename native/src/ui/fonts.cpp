#include "glt/ui/fonts.hpp"

#include "glt/core/strings.hpp"

#include <windows.h>

namespace glt::ui::fonts {
namespace {

Fonts g_fonts;
int g_current_scale = -1;
std::vector<std::string> g_diagnostics;

struct Candidate {
    const wchar_t* file;
    const char* label;
};

// Probed in order; the first readable file wins. Segoe UI Variable is the
// Windows 11 default and looks noticeably better, but only ships on 11+.
constexpr Candidate kSans[] = {
    {L"C:\\Windows\\Fonts\\segoeuib.ttf", "Segoe UI Bold"},
    {L"C:\\Windows\\Fonts\\segoeui.ttf", "Segoe UI"},
    {L"C:\\Windows\\Fonts\\tahoma.ttf", "Tahoma"},
};
constexpr Candidate kSemibold[] = {
    {L"C:\\Windows\\Fonts\\seguisb.ttf", "Segoe UI Semibold"},
    {L"C:\\Windows\\Fonts\\segoeuib.ttf", "Segoe UI Bold"},
};
constexpr Candidate kMono[] = {
    {L"C:\\Windows\\Fonts\\consola.ttf", "Consolas"},
    {L"C:\\Windows\\Fonts\\cour.ttf", "Courier New"},
};
constexpr Candidate kSymbol[] = {
    {L"C:\\Windows\\Fonts\\seguisym.ttf", "Segoe UI Symbol"},
    {L"C:\\Windows\\Fonts\\seguiemj.ttf", "Segoe UI Emoji"},
};
constexpr Candidate kIcons[] = {
    {L"C:\\Windows\\Fonts\\segmdl2.ttf", "Segoe MDL2 Assets"},
    {L"C:\\Windows\\Fonts\\seguisym.ttf", "Segoe UI Symbol"},
};

// Returns the first readable file in a candidate list as a UTF-8 path, or "".
//
// The conversion matters: AddFontFromFileTTF takes a `const char*` path in
// 1.91.x (the wchar_t overload only exists in some forks), so the path is
// converted once here rather than at every call site.
std::string first_existing(const Candidate* list, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (str::file_exists(list[i].file)) return str::to_utf8(std::wstring_view(list[i].file));
    }
    return {};
}

void note_missing(const char* what, const Candidate* list, std::size_t n) {
    std::string msg = std::string("font: no face found for ") + what + " (tried";
    for (std::size_t i = 0; i < n; ++i) {
        if (i) msg += ", ";
        msg += list[i].label;
    }
    msg += ")";
    g_diagnostics.push_back(std::move(msg));
}

// Fills in the three fields this app actually wants, and nothing else.
//
// DO NOT memset THIS STRUCT.
// -------------------------
// ImFontConfig's constructor memset()s itself and then sets the defaults that
// make a font rasterise at all:
//
//     GlyphMaxAdvanceX      = FLT_MAX
//     RasterizerMultiply    = 1.0f
//     RasterizerDensity     = 1.0f
//     FontDataOwnedByAtlas  = true
//
// A bare memset(this, 0, sizeof(*this)) here — which is what this function used
// to do — silently destroys every one of them, and ImGui's own guard
// (imgui_draw.cpp, `IM_ASSERT(RasterizerDensity > 0.0f && "Is ImFontConfig
// struct correctly initialized?")`) compiles out in a release build. The
// consequences, all of them invisible at compile time and at run time:
//
//   RasterizerDensity == 0   -> font_size = size * 0 == 0
//                            -> stbtt_ScaleForPixelHeight(0) -> every glyph
//                               metric is 0, so nothing is ever rasterised.
//   RasterizerMultiply == 0  -> the coverage lookup table multiplies every
//                               glyph's alpha to 0, so what IS rasterised
//                               is invisible.
//   GlyphMaxAdvanceX == 0    -> ImClamp(advance, 0, 0) == 0 in AddGlyph(), so
//                               every glyph advances 0 px and all text stacks
//                               on top of itself at the same x.
//   FontDataOwnedByAtlas == false -> the atlas never frees the TTF buffers.
//
// The net effect is a UI whose window, cards and buttons all draw perfectly and
// whose entire text layer is blank. Default-construct and overwrite only what
// you intend to change.
void base_config(ImFontConfig& cfg) {
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
}

// Adds one face at one size, with no merging. Used for the semibold weights,
// where neither the symbol nor the icon face has a matching bold and merging
// would render the icons heavy and blurry.
ImFont* add_plain(ImFontAtlas* atlas, float px, const Candidate* list, std::size_t n,
                  const char* name) {
    const std::string path = first_existing(list, n);
    if (path.empty()) {
        note_missing(name, list, n);
        return nullptr;
    }

    ImFontConfig cfg;
    base_config(cfg);

    ImFont* f = atlas->AddFontFromFileTTF(path.c_str(), px, &cfg, kGlyphRanges);
    if (!f) {
        g_diagnostics.push_back(std::string("font: rasterise failed for ") + list[0].label);
    }
    return f;
}

// Adds the primary face at one size AND merges the symbol + icon faces into it,
// which is what makes the ✓ ✗ ⚠ ● ⇄ and the MDL2 glyphs available at that size.
ImFont* add_with_symbols(ImFontAtlas* atlas, float px, const Candidate* list, std::size_t n,
                         const char* name) {
    const std::string path = first_existing(list, n);
    if (path.empty()) {
        note_missing(name, list, n);
        return nullptr;
    }

    ImFontConfig cfg;
    base_config(cfg);

    ImFont* f = atlas->AddFontFromFileTTF(path.c_str(), px, &cfg, kGlyphRanges);
    if (!f) {
        g_diagnostics.push_back(std::string("font: rasterise failed for ") + list[0].label);
        return nullptr;
    }

    // Merge the symbol + icon faces in. The array (rather than a
    // pointer-to-array) is what lets the candidate list decay to Candidate*.
    for (const Candidate* extras : {kSymbol, kIcons}) {
        const std::string extra = first_existing(extras, 2);
        if (extra.empty()) continue;

        ImFontConfig merge;
        base_config(merge);
        merge.MergeMode = true;

        atlas->AddFontFromFileTTF(extra.c_str(), px, &merge, kGlyphRanges);
    }
    return f;
}

} // namespace

Rebuild rebuild(int dpi_scale) {
    if (dpi_scale < kMinScale) dpi_scale = kMinScale;
    if (dpi_scale > kMaxScale) dpi_scale = kMaxScale;
    dpi_scale = ((dpi_scale + kScaleStep / 2) / kScaleStep) * kScaleStep;

    if (dpi_scale == g_current_scale) return Rebuild::Unchanged;
    g_current_scale = dpi_scale;

    g_diagnostics.clear();
    g_fonts = Fonts{};

    const float s = static_cast<float>(dpi_scale) / 100.0f;

    // ImGuiIO::Fonts is a POINTER in 1.91.x, not a reference — &io.Fonts
    // would be ImFontAtlas**, which is exactly one level too many.
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();

    // Larger by default than the legacy 14/12 px body sizes: the redesigned UI
    // relies on comfortable reading, and the scale dropdown still reaches down
    // to the small sizes for users who want it tighter.
    g_fonts.body       = add_with_symbols(atlas, 16.0f * s, kSans, IM_ARRAYSIZE(kSans),
                                          "Segoe UI 16");
    g_fonts.small_text = add_with_symbols(atlas, 14.0f * s, kSans, IM_ARRAYSIZE(kSans),
                                          "Segoe UI 14");
    g_fonts.nav        = add_with_symbols(atlas, 17.0f * s, kSans, IM_ARRAYSIZE(kSans),
                                          "Segoe UI 17");
    // The heading face renders ICONS as well as text (setting_row draws its icon
    // glyph with it, and nav_active aliases to it). It therefore has to merge
    // the symbol + MDL2 faces exactly like nav/body do — add_plain() here is
    // what made every card icon and the active-nav icon fall back to '?' even
    // for BMP symbols like U+2699, because Segoe UI Semibold alone does not
    // contain them. Merging renders the icons monochrome at the correct weight,
    // which is the intended look anyway.
    g_fonts.heading    = add_with_symbols(atlas, 17.0f * s, kSemibold, IM_ARRAYSIZE(kSemibold),
                                          "Segoe UI Semibold 17");
    g_fonts.title      = add_with_symbols(atlas, 26.0f * s, kSemibold, IM_ARRAYSIZE(kSemibold),
                                          "Segoe UI Semibold 26");
    g_fonts.app_title  = add_with_symbols(atlas, 28.0f * s, kSemibold, IM_ARRAYSIZE(kSemibold),
                                          "Segoe UI Semibold 28");
    g_fonts.mono       = add_with_symbols(atlas, 15.0f * s, kMono, IM_ARRAYSIZE(kMono),
                                          "Consolas 15");
    g_fonts.mono_small = add_with_symbols(atlas, 14.0f * s, kMono, IM_ARRAYSIZE(kMono),
                                          "Consolas 14");

    // The active nav item reuses the semibold face — there is no separate bold
    // 15 px entry, and synthesising one would just be the same bytes.
    g_fonts.nav_active = g_fonts.heading;

    if (g_fonts.valid()) return Rebuild::Ok;

    // Total failure (stripped Windows, redirected Fonts folder). Hand back the
    // default face so the app still runs and says so, rather than crashing or
    // rendering invisible text.
    g_diagnostics.push_back(
        "font: falling back to the ImGui default face - text will look wrong");
    atlas->Clear();

    ImFontConfig cfg;
    base_config(cfg);
    cfg.SizePixels = 15.0f * s;

    g_fonts.body = atlas->AddFontDefault(&cfg);
    g_fonts.small_text = g_fonts.body;
    g_fonts.nav = g_fonts.body;
    g_fonts.heading = g_fonts.body;
    g_fonts.nav_active = g_fonts.body;
    g_fonts.title = g_fonts.body;
    g_fonts.app_title = g_fonts.body;
    g_fonts.mono = g_fonts.body;
    g_fonts.mono_small = g_fonts.body;
    return Rebuild::Fallback;
}

const Fonts& current() { return g_fonts; }

ImFont* body() {
    return g_fonts.body ? g_fonts.body : ImGui::GetFont();
}

const std::vector<std::string>& diagnostics() { return g_diagnostics; }

} // namespace glt::ui::fonts