// ─────────────────────────────────────────────────────────────────────────────
// keymaps.cpp tests — the <Tex> matcher and the SwitchOperation stripper.
//
// These are the two functions where a divergence from Python's regex semantics
// would silently disable the blind fix. Both are tested against the REAL
// smk.conf element layout, which puts comm="…" on the FOLLOWING line behind a
// tab:
//
//   <Tex id="10004" width="1024" … path="225154410"
//       comm="信箱"/>
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/keymaps.hpp"

#include <cstring>
#include <string>

using namespace glt::keymaps;

namespace {

// Verbatim element from a live smk.conf, with its real line break and tab.
std::string tex_element(int id, const char* comm) {
    std::string s = "<Tex id=\"" + std::to_string(id) +
                    "\" width=\"1024\" height=\"1024\" max_bufsize=\"20480\" "
                    "c1=\"236855296\" c2=\"202247694\" cut_width=\"40\" "
                    "cut_height=\"46\" path=\"225154410\"\n\tcomm=\"";
    s += comm;
    s += "\"/>\n";
    return s;
}

} // namespace

// ── <Tex> discovery ──────────────────────────────────────────────────────────

GLT_TEST(TexIds, enumerates_all_ids) {
    std::string doc = "<root>" + tex_element(10001, "a") + tex_element(10002, "b") +
                      tex_element(10046, "c") + "</root>";
    const auto ids = tex_ids_in_text(doc);

    CHECK_EQ(ids.size(), std::size_t{3});
    CHECK_EQ(ids[0], 10001);
    CHECK_EQ(ids[1], 10002);
    CHECK_EQ(ids[2], 10046);
}

GLT_TEST(TexIds, deduplicates) {
    std::string doc = tex_element(10001, "a") + tex_element(10001, "a");
    CHECK_EQ(tex_ids_in_text(doc).size(), std::size_t{1});
}

GLT_TEST(TexIds, empty_and_malformed_documents) {
    CHECK_EQ(tex_ids_in_text("").size(), std::size_t{0});
    CHECK_EQ(tex_ids_in_text("<root></root>").size(), std::size_t{0});
    // No digits between the quotes: must not match, must not loop forever.
    CHECK_EQ(tex_ids_in_text("<Tex id=\"\" width=\"1\"/>").size(), std::size_t{0});
}

// ── <Tex> blinding ───────────────────────────────────────────────────────────

GLT_TEST(Blind, removes_a_multiline_element_and_reports_comm) {
    std::string doc = "<root>" + tex_element(10004, "\xE9\x82\xAE\xE7\xAE\xB1") +
                      tex_element(10005, "keep me") + "</root>";

    TexMatch m;
    CHECK(blind_tex_in_text(doc, 10004, &m));

    CHECK_EQ(m.comm, std::string("\xE9\x82\xAE\xE7\xAE\xB1")); // 信箱
    CHECK(doc.find("id=\"10004\"") == std::string::npos);
    CHECK(doc.find("10005") != std::string::npos); // neighbours untouched
    CHECK(doc.rfind("<root>", 0) == 0);
    CHECK(doc.rfind("</root>") == doc.size() - 7);
}

GLT_TEST(Blind, is_idempotent) {
    std::string doc = "<root>" + tex_element(10046, "skill") + "</root>";

    CHECK(blind_tex_in_text(doc, 10046));
    // Second attempt must report "absent" so the UI can say so, not corrupt
    // the document.
    CHECK(!blind_tex_in_text(doc, 10046));
    CHECK(doc.find("id=\"10046\"") == std::string::npos);
}

GLT_TEST(Blind, does_not_match_a_longer_id) {
    // '<Tex id="100046"' must NOT be removed when asked to blind 10004.
    std::string doc = "<root>" + tex_element(100046, "unrelated") + "</root>";

    CHECK(!blind_tex_in_text(doc, 10004));
    CHECK(doc.find("100046") != std::string::npos);
}

GLT_TEST(Blind, removes_both_tex_series) {
    // 10xxx primary + 30xxx twin, which is the pair the engine matches as two
    // bindings for the same switch.
    std::string doc =
        "<root>" + tex_element(10046, "skill") + tex_element(30046, "skill twin") +
        tex_element(14600, "unrelated") + "</root>";

    CHECK(blind_tex_in_text(doc, 10046));
    CHECK(blind_tex_in_text(doc, 30046));
    CHECK(doc.find("10046") == std::string::npos);
    CHECK(doc.find("30046") == std::string::npos);
    CHECK(doc.find("14600") != std::string::npos);
}

GLT_TEST(Blind, keeps_the_element_order_of_the_remainder) {
    std::string doc = "<root>" + tex_element(10001, "a") + tex_element(10004, "b") +
                      tex_element(10005, "c") + "</root>";

    CHECK(blind_tex_in_text(doc, 10004));

    const auto a = doc.find("10001");
    const auto c = doc.find("10005");
    CHECK(a != std::string::npos);
    CHECK(c != std::string::npos);
    CHECK(a < c);
}

GLT_TEST(Blind, handles_a_self_closing_element_with_no_comm) {
    std::string doc = "<root><Tex id=\"10004\" width=\"1\"/></root>";
    TexMatch m;
    CHECK(blind_tex_in_text(doc, 10004, &m));
    CHECK_EQ(m.comm, std::string(""));
    CHECK_EQ(doc, std::string("<root></root>"));
}

GLT_TEST(Blind, does_not_treat_a_greater_than_inside_an_attribute_as_the_end) {
    // comm="a>b" would terminate the tag early if the scanner did not honour
    // quoted attribute values.
    std::string doc = "<root><Tex id=\"10004\" comm=\"a>b\"/></root>";
    TexMatch m;
    CHECK(blind_tex_in_text(doc, 10004, &m));
    CHECK_EQ(m.comm, std::string("a>b"));
}

// ── SwitchOperation stripping ────────────────────────────────────────────────

GLT_TEST(Flatten, strips_the_self_closing_form) {
    const std::string xml =
        "<KeyMapSolution><Key a=\"1\"/><SwitchOperation id=\"9\"/></KeyMapSolution>";
    const std::string out = flatten_in_place(xml);

    CHECK(out.find("SwitchOperation") == std::string::npos);
    CHECK(out.find("<Key a=\"1\"/>") != std::string::npos);
}

GLT_TEST(Flatten, strips_the_paired_form_including_its_body) {
    const std::string xml =
        "<KeyMapSolution><Key a=\"1\"/><SwitchOperation id=\"9\">\n  <Rule x=\"1\"/>\n"
        "</SwitchOperation><Key b=\"2\"/></KeyMapSolution>";
    const std::string out = flatten_in_place(xml);

    CHECK(out.find("SwitchOperation") == std::string::npos);
    CHECK(out.find("Rule") == std::string::npos);
    CHECK(out.find("<Key a=\"1\"/>") != std::string::npos);
    CHECK(out.find("<Key b=\"2\"/>") != std::string::npos);
}

GLT_TEST(Flatten, strips_several) {
    const std::string xml =
        "<r><SwitchOperation/><Key a=\"1\"/>"
        "<SwitchOperation><Rule/></SwitchOperation>"
        "<SwitchOperation x=\"1\"/>"
        "<Key b=\"2\"/></r>";
    const std::string out = flatten_in_place(xml);

    CHECK(out.find("SwitchOperation") == std::string::npos);
    CHECK(out.find("Rule") == std::string::npos);
    CHECK(out.find("<Key a=\"1\"/>") != std::string::npos);
    CHECK(out.find("<Key b=\"2\"/>") != std::string::npos);
}

GLT_TEST(Flatten, preserves_the_wrapper_and_all_other_fields) {
    // flatten_in_place is used for the mode-145 blobs, where re-wrapping would
    // discard fields GameLoop wrote itself.
    const std::string xml =
        "<KeyMapSolution SolutionID=\"x\" SolutionName=\"Multi Player\" "
        "InputType=\"1\"><Key a=\"1\"/></KeyMapSolution>";
    const std::string out = flatten_in_place(xml);

    CHECK_EQ(out, xml);
}

GLT_TEST(Flatten, does_not_touch_a_similarly_named_element) {
    // <SwitchOperations> is a different element and must survive.
    const std::string xml = "<r><SwitchOperations a=\"1\"/><SwitchOperation/></r>";
    const std::string out = flatten_in_place(xml);

    CHECK(out.find("<SwitchOperations a=\"1\"/>") != std::string::npos);
    CHECK(out.find("<SwitchOperation/>") == std::string::npos);
}

GLT_TEST(Flatten, already_flat_input_is_unchanged) {
    const std::string xml = "<KeyMapSolution><Key a=\"1\"/><Key b=\"2\"/></KeyMapSolution>";
    CHECK_EQ(flatten_in_place(xml), xml);
}

GLT_TEST(Flatten, handles_empty_and_unterminated) {
    CHECK_EQ(flatten_in_place(""), std::string(""));

    // An unterminated paired element must not eat the rest of the document.
    const std::string xml = "<r><SwitchOperation><Rule/>";
    const std::string out = flatten_in_place(xml);
    CHECK(out.find("Rule") != std::string::npos);
}

// ── flatten_blob ─────────────────────────────────────────────────────────────

GLT_TEST(FlattenBlob, wraps_and_strips) {
    const std::string official =
        "<x><SwitchOperation id=\"1\"/><Key a=\"1\"/></x>";
    const std::string out = flatten_blob(official, "1063553", "DMZ");

    CHECK(out.find("SwitchOperation") == std::string::npos);
    CHECK(out.rfind("<KeyMapSolution", 0) == 0);
    CHECK(out.size() >= 17);
    CHECK(out.compare(out.size() - std::strlen("</KeyMapSolution>"),
                      std::strlen("</KeyMapSolution>"),
                      "</KeyMapSolution>") == 0);

    // SolutionID carries the JSON blob, XML-escaped.
    CHECK(out.find("&quot;id&quot;:&quot;1063553&quot;") != std::string::npos);
    CHECK(out.find("SolutionName=\"DMZ\"") != std::string::npos);
    CHECK(out.find("InputType=\"1\"") != std::string::npos);
    CHECK(out.find("SilentUpdate=\"0\"") != std::string::npos);
}

GLT_TEST(FlattenBlob, escapes_a_hostile_solution_name) {
    const std::string out = flatten_blob("<x/>", "1", R"(a"b<c)");
    CHECK(out.find("a\"b<c>") == std::string::npos);
    CHECK(out.find("&quot;") != std::string::npos);
}

// ── Key naming ───────────────────────────────────────────────────────────────

GLT_TEST(Keys, match_the_verified_schema) {
    // Verified against a live Configs.db. A typo in any of these turns a fix
    // into a silent no-op that reads to the user as "nothing happened".
    constexpr const char* pkg = "com.activision.callofduty.shooter";

    CHECK_EQ(mode_key(pkg, "145"),
             std::string("keymap_mode.unlogin.com.activision.callofduty.shooter.145"));
    CHECK_EQ(blob_key(pkg, "146", "146"),
             std::string("keymap_data.unlogin.com.activision.callofduty.shooter.146.146"));
    CHECK_EQ(official_blob_key(pkg, "531686", "1063553"),
             std::string(
                 "offcial_keymap_data2.com.activision.callofduty.shooter.531686.1063553"));
    CHECK_EQ(official_config_key(pkg),
             std::string("offcial_config2.com.activision.callofduty.shooter"));
    CHECK_EQ(lastmodeid_key(pkg),
             std::string("lastmodeid.unlogin.com.activision.callofduty.shooter"));
    CHECK_EQ(recentlyused_key(pkg),
             std::string("recentlyused_mode.com.activision.callofduty.shooter"));
    CHECK_EQ(smk_key(pkg),
             std::string("androws.com.activision.callofduty.shooter.smk"));
    CHECK_EQ(local_user_config_key(pkg),
             std::string("androws.com.activision.callofduty.shooter.local_user_config"));
}

GLT_TEST(Keys, preserve_the_offcial_misspelling) {
    // GameLoop's own typo. Correcting it to "official" would address a row
    // that does not exist and, for the guarded rows, silently bypass the
    // trigger that is supposed to keep us out.
    constexpr const char* pkg = "p";
    const std::string k = official_blob_key(pkg, "146", "146");
    CHECK(k.rfind("offcial_", 0) == 0);
    CHECK(k.find("official_") == std::string::npos);
}

// ── tex_to_switch ────────────────────────────────────────────────────────────

GLT_TEST(TexToSwitch, subtracts_ten_thousand) {
    CHECK_EQ(tex_to_switch(10004), 4);
    CHECK_EQ(tex_to_switch(10046), 46);
    CHECK_EQ(tex_to_switch(30046), 20046);
}