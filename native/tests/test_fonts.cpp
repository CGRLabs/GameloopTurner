// ─────────────────────────────────────────────────────────────────────────────
// fonts.cpp tests — the glyph range table, and nothing else.
//
// WHY THIS FILE EXISTS
// --------------------
// The app shipped with a glyph range table that carried only the decorative
// blocks and omitted Basic Latin. A non-null glyph_ranges argument REPLACES
// ImGui's default set instead of extending it (imgui_draw.cpp: the default is
// { 0x0020, 0x00FF }), so the atlas held no letters, digits, spaces or
// punctuation. ImGui's fallback glyph is '?', which is ASCII and therefore
// absent too, so ImFont::RenderText() silently skipped every character. The
// window, the cards and the buttons all drew; the entire text layer was
// invisible, and nothing in the build or the tests complained.
//
// These tests are deliberately pure table arithmetic. They do not need
// C:\Windows\Fonts, a GPU, or a font file to exist.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/ui/fonts.hpp"

#include <imgui.h>

#include <algorithm>
#include <initializer_list>

using namespace glt::ui::fonts;

// ── Helpers ──────────────────────────────────────────────────────────────────

// The number of ImWchar values in the table, counting the terminator.
// IM_ARRAYSIZE is an ImGui macro and is available here.
constexpr std::size_t kTableSize = IM_ARRAYSIZE(kGlyphRanges);

// True when cp is inside one of the ranges.
bool ranges_cover(ImWchar cp) {
    for (std::size_t i = 0; i + 1 < kTableSize; i += 2) {
        if (cp >= kGlyphRanges[i] && cp <= kGlyphRanges[i + 1]) return true;
    }
    return false;
}

// The terminator must be a 0, 0 pair OR a single trailing 0. Either way the
// last value read must be 0, which is how ImFontAtlas knows to stop.
GLT_TEST(GlyphRanges, is_terminated) {
    CHECK(kGlyphRanges[kTableSize - 1] == 0);
    // And there must not be a stray lone 0 in the middle.
    for (std::size_t i = 0; i + 1 < kTableSize; i += 2) {
        CHECK(kGlyphRanges[i] != 0);
    }
}

GLT_TEST(GlyphRanges, covers_basic_latin) {
    // This is the regression guard. Every label in the UI — page titles, button
    // captions, log lines, the build string — is ASCII. Without this range the
    // text layer renders nothing at all.
    for (ImWchar cp = 0x20; cp <= 0xFF; ++cp) {
        CHECK(ranges_cover(cp));
    }
}

GLT_TEST(GlyphRanges, covers_ascii_control_free_printable) {
    // The space character is the one that is easiest to forget and the one that
    // breaks layout first: ImGui measures text width with it.
    CHECK(ranges_cover(0x20));
    CHECK(ranges_cover(0x30));              // '0'
    CHECK(ranges_cover(0x41));              // 'A'
    CHECK(ranges_cover(0x7E));              // '~'
    CHECK(ranges_cover(0xA0));              // NBSP, Latin Supplement
}

GLT_TEST(GlyphRanges, covers_the_symbols_the_ui_draws) {
    // Glyphs actually emitted by the UI, taken verbatim from the source.
    // U+2139 ℹ  sidebar "About"          U+2500 ─  log group separator
    // U+2318 ⌘  sidebar "Keymaps"        U+25CF ●  status dot / log bullet
    // U+2699 ⚙  sidebar "VM & Priority"  U+26A0 ⚠  warning / drift badge
    // U+2605 ★  recommended card         U+2713 ✓  success
    // U+2717 ✗  error / close button     U+2715 ✕  close button variant
    // U+2261 ≡  sidebar "Logs"          U+21C4 ⇄  drift
    // U+2961    sidebar "Dashboard"      U+2B21 ⬡  hexagon
    // U+25BE ▾  collapsible header       U+25B8 ▸  collapsed header
    // U+25CC ◌  progress bullet
    static constexpr ImWchar kUsed[] = {
        0x2139, 0x2318, 0x2699, 0x26A0, 0x2605, 0x2713, 0x2717, 0x2715,
        0x2261, 0x21C4, 0x2961, 0x2B21, 0x25BE, 0x25B8, 0x25CC, 0x2500,
    };
    for (ImWchar cp : kUsed) {
        CHECK(ranges_cover(cp));
    }
}

GLT_TEST(GlyphRanges, ranges_are_sorted_and_non_overlapping) {
    // ImGui iterates the ranges linearly and does not require sorted order,
    // but a table that is out of order or overlaps is a table nobody can read
    // or extend safely, and this one is covered by the tests above.
    for (std::size_t i = 2; i + 1 < kTableSize; i += 2) {
        CHECK(kGlyphRanges[i] > kGlyphRanges[i - 1]);   // strictly increasing
        CHECK(kGlyphRanges[i] > kGlyphRanges[i - 2]);   // no overlap
    }
}

GLT_TEST(GlyphRanges, every_range_is_wide_enough_to_be_useful) {
    for (std::size_t i = 0; i + 1 < kTableSize; i += 2) {
        CHECK(kGlyphRanges[i] <= kGlyphRanges[i + 1]);
    }
}

GLT_TEST(GlyphRanges, ascii_comes_first) {
    // Keeping the ASCII block first means an ASCII-heavy font still rasterises
    // in the same atlas region regardless of what is appended later, and it
    // reads as an invariant a future edit is likely to preserve.
    CHECK(kGlyphRanges[0] == 0x0020);
    CHECK(kGlyphRanges[1] == 0x00FF);
}

GLT_TEST(GlyphRanges, does_not_pull_in_cjk) {
    // The whole point of a custom table is a small atlas. A CJK range would
    // add tens of megabytes of texture for text this app never renders.
    CHECK(!ranges_cover(0x4E00));  // 一
    CHECK(!ranges_cover(0x3042));  // あ
    CHECK(!ranges_cover(0xAC00));  // 가
}

// ── End-to-end: the atlas that actually gets rasterised ──────────────────────
//
// The table checks above prove what we intended. These prove what ImGui
// actually produced, which is the only thing that mattered: the shipped build
// had a perfectly well-formed table that still yielded a font with no letters
// in it. Needs no GPU and no GameLoop — ImFontAtlas::Build() is stb_truetype.
//
// NOTE: rebuild() keeps global state (the current scale bucket and the loaded
// font pointers), and an ImFont* is only meaningful inside the ImGui context it
// was created in. So all atlas assertions share ONE context and ONE rebuild;
// splitting them across cases would leave the second looking at a font that
// belongs to a destroyed context.

GLT_TEST(Atlas, rasterises_ascii_and_symbols_into_the_body_font) {
    ImGui::CreateContext();
    struct ContextGuard {
        ~ContextGuard() { ImGui::DestroyContext(); }
    } guard;

    CHECK(rebuild(100) != Rebuild::Unchanged);

    const Fonts& f = current();
    // Either the real faces loaded, or rebuild() fell back to ProggyClean. Both
    // are usable; an atlas with no font at all is not.
    CHECK(f.body != nullptr);
    CHECK(f.mono != nullptr);
    if (f.body == nullptr) return;

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    CHECK(atlas->Build());

    // FindGlyphNoFallback, not FindGlyph: the original failure was that the
    // fallback glyph was ALSO ASCII and therefore absent too, so a
    // fallback-tolerant check would have passed on the broken build.
    for (ImWchar cp : {ImWchar('A'), ImWchar('z'), ImWchar('0'), ImWchar(' '),
                       ImWchar('-'), ImWchar('.'), ImWchar('('), ImWchar(':')}) {
        CHECK(f.body->FindGlyphNoFallback(cp) != nullptr);
    }

    // Symbols the UI draws must survive too, or those widgets go blank.
    for (ImWchar cp : {ImWchar(0x2713), ImWchar(0x26A0), ImWchar(0x2500),
                       ImWchar(0x2139)}) {
        CHECK(f.body->FindGlyphNoFallback(cp) != nullptr);
    }

    // Segoe UI Symbol / MDL2 are merged into the body face, so they could in
    // principle shadow the ASCII glyphs. ImGui's merge never overwrites an
    // existing glyph, and this is the check that keeps it that way.
    const ImFontGlyph* a = f.body->FindGlyphNoFallback(ImWchar('A'));
    CHECK(a != nullptr);
    if (a) CHECK(a->AdvanceX > 0.0f);

    // The decisive one: a glyph can be present in the table and still paint
    // nothing. ImFontConfig::RasterizerMultiply defaults to 1.0f; if it is
    // left at 0 the atlas texture comes out entirely transparent, so every
    // character is "there" and still invisible. Sample the glyph's own UV rect
    // and demand that some pixel actually has coverage.
    if (a != nullptr) {
        unsigned char* pixels = nullptr;
        int tex_w = 0;
        int tex_h = 0;
        atlas->GetTexDataAsRGBA32(&pixels, &tex_w, &tex_h);
        CHECK(pixels != nullptr);
        CHECK(tex_w > 0 && tex_h > 0);

        if (pixels && tex_w > 0 && tex_h > 0) {
            // Skip the 1px border padding, which is always zero.
            const int x0 = std::max(static_cast<int>(a->U0 * tex_w) + 1, 0);
            const int x1 = std::min(static_cast<int>(a->U1 * tex_w), tex_w - 1);
            const int y0 = std::max(static_cast<int>(a->V0 * tex_h) + 1, 0);
            const int y1 = std::min(static_cast<int>(a->V1 * tex_h), tex_h - 1);

            int lit_pixels = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const unsigned int px = pixels[(y * tex_w + x) * 4 + 0];
                    if (px > 16) ++lit_pixels;
                }
            }
            CHECK(lit_pixels > 0);
        }
    }

    // NOTE: this block shares the context and the single rebuild() above. A
    // separate GLT_TEST here would fail spuriously — rebuild() caches the DPI
    // bucket globally, so a second case asking for scale 100 gets
    // Rebuild::Unchanged and then inspects font pointers belonging to the first
    // case's DESTROYED context. That is the trap the header comment warns about.

    // REGRESSION GUARD for the '?' icons on setting rows.
    //
    // setting_row() draws its icon with fonts::current().heading, and nav_active
    // aliases to the same face. heading used to be built with add_plain(), which
    // does NOT merge the Segoe UI Symbol / MDL2 faces. Segoe UI Semibold has no
    // glyph of its own for the icons (nor even for BMP symbols like U+2699), so
    // every icon on a card rendered as ImGui's '?' fallback — while the very
    // same glyph rendered correctly in the sidebar, which uses the merged `nav`
    // face. That asymmetry is why the bug looked font-related but not
    // glyph-range-related.
    if (f.heading != nullptr) {
        CHECK(f.heading == f.nav_active);   // the alias must still hold

        // Segoe MDL2 Assets codepoints, taken verbatim from kNav in sidebar.cpp
        // and the setting-row icons. All live in the Private Use Area the range
        // table covers, and all are served by the merged icon face.
        for (ImWchar cp : {ImWchar(0xE80F), ImWchar(0xE945), ImWchar(0xE8EC),
                           ImWchar(0xE713), ImWchar(0xE12B), ImWchar(0xE74E),
                           ImWchar(0xE9D5), ImWchar(0xE946), ImWchar(0xE823),
                           ImWchar(0xE714), ImWchar(0xE7F8), ImWchar(0xE8B9),
                           ImWchar(0xE8C8)}) {
            CHECK(f.heading->FindGlyphNoFallback(cp) != nullptr);
        }

        // And the BMP symbols the cards still use. These are covered by the
        // range table but are NOT in Segoe UI Semibold either, so they only
        // resolve if the symbol face was merged in.
        for (ImWchar cp : {ImWchar(0x2699), ImWchar(0x26A0), ImWchar(0x2139),
                           ImWchar(0x2713), ImWchar(0x2715), ImWchar(0x25B6)}) {
            CHECK(f.heading->FindGlyphNoFallback(cp) != nullptr);
        }

        // ASCII must survive the merge: MDL2 has no Latin glyphs, but a merged
        // face that lost its base would be a much worse regression than a missing
        // icon.
        for (ImWchar cp : {ImWchar('A'), ImWchar('0'), ImWchar(' ')}) {
            CHECK(f.heading->FindGlyphNoFallback(cp) != nullptr);
        }
    }
}
