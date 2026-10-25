// ─────────────────────────────────────────────────────────────────────────────
// layout.cpp tests — the widget sizing contracts that decide whether a card is
// visible at all.
//
// WHY THIS FILE EXISTS
// --------------------
// theme::begin_card() opens a child window with ImVec2(0, 0). For BeginChild a
// size of 0 means "take the parent's REMAINING height" (imgui.cpp,
// BeginChildEx: size_default.y is size_avail.y unless AutoResizeY is set).
//
//   * On the Dashboard, a card's parent is an AutoResizeY wrapper sitting in a
//     table cell. An auto-resizing window has no height yet while its contents
//     are still being submitted, so the card measured its available height as
//     ~0 and every one of the six cards collapsed to a 2 px sliver.
//   * On the Performance page the same card sits inside a plain scroll child
//     with a large fixed height, so it filled that instead — the bug was
//     invisible on every page except the one it broke.
//
// Neither failure produced an error, a crash or a warning; both just drew a
// blank page. These tests reproduce the nesting and assert the committed height
// in both directions: big enough to be seen, and not swallowing the parent.
//
// No GPU is needed. ImGui's NewFrame/EndFrame are pure CPU, so the automatic fit
// sizes are committed after EndFrame() and read back on the following frame.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include <imgui.h>

#include "glt/ui/theme.hpp"

namespace {

// A fresh context with a sane display size and a built atlas, so NewFrame()
// does not need a font loader or a GPU.
struct HeadlessGui {
    HeadlessGui() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        // CreateContext() leaves DisplaySize at (-1,-1), which NewFrame()
        // rejects. Without this every child window would measure zero.
        io.DisplaySize = ImVec2(1280.0f, 800.0f);
        io.Fonts->AddFontDefault();
        io.Fonts->Build();
    }
    ~HeadlessGui() { ImGui::DestroyContext(); }
};

// Draws one card in a parent configured like the named page, returns the
// largest height the card reported across two frames.
//
// Two frames are needed: an auto-resizing window's size is only committed at
// the END of the frame, so the value read back is valid on the second pass.
float measure_card(bool wrap_in_autoresize, bool inside_table, int rows,
                   const char* parent_id, const char* card_id) {
    HeadlessGui gui;
    const float col_w = 420.0f;
    float best = 0.0f;

    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();

        ImGui::BeginChild(parent_id, ImVec2(0, 0));
        if (inside_table) {
            ImGui::BeginTable("##grid", 2,
                              ImGuiTableFlags_SizingFixedSame |
                                  ImGuiTableFlags_PadOuterX);
            ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, col_w);
            ImGui::TableSetupColumn("r", ImGuiTableColumnFlags_WidthFixed, col_w);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
        }

        float card_h = 0.0f;
        if (wrap_in_autoresize) {
            ImGui::BeginChild(card_id, ImVec2(col_w, 0), ImGuiChildFlags_AutoResizeY);
            glt::ui::theme::begin_card("Card");
        } else {
            glt::ui::theme::begin_card("Card");
        }

        for (int i = 0; i < rows; ++i) ImGui::TextUnformatted("line of body text");
        card_h = ImGui::GetWindowSize().y;

        glt::ui::theme::end_card();
        if (wrap_in_autoresize) ImGui::EndChild();
        if (inside_table) ImGui::EndTable();

        ImGui::EndChild();
        ImGui::EndFrame();

        if (card_h > best) best = card_h;
    }

    return best;
}

} // namespace

GLT_TEST(Layout, dashboard_card_in_an_autoresize_parent_keeps_its_height) {
    // The dashboard's exact shape: transparent page child -> table ->
    // AutoResizeY wrapper -> begin_card. This is the one that collapsed.
    const float h = measure_card(/*wrap_in_autoresize=*/true,
                                 /*inside_table=*/true, /*rows=*/8,
                                 "##dash_page", "##c_gl");

    // A card with eight rows plus the 14 px card padding cannot be ~0.
    CHECK(h > 30.0f);
}

GLT_TEST(Layout, card_in_a_plain_parent_sizes_to_content) {
    // The Performance page's shape: plain scroll child -> begin_card. Here the
    // card must NOT swallow the parent's remaining height either, or one card
    // would take the whole page and push the rest off-screen.
    const float h = measure_card(/*wrap_in_autoresize=*/false,
                                 /*inside_table=*/false, /*rows=*/8,
                                 "##perf_scroll", "##card_plain");

    CHECK(h > 30.0f);
    // The parent is an 800 px-tall window; eight text rows is a long way short
    // of that. A regression to "fill the parent" shows up here.
    CHECK(h < 500.0f);
}

GLT_TEST(Layout, autosize_card_height_tracks_its_content) {
    // Not merely non-zero: the card measures what is inside it. A card with
    // twelve rows must report a taller window than one with two, which is what
    // makes AutoResizeY the correct semantics rather than a fixed-height guess.
    const float small = measure_card(true, false, 2, "##p_small", "##c_s");
    const float large = measure_card(true, false, 12, "##p_large", "##c_l");

    CHECK(large > small);
    CHECK(large - small > 30.0f);  // ten rows at ~17 px each is ~170 px
}
