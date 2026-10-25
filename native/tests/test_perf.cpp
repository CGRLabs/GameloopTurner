// ─────────────────────────────────────────────────────────────────────────────
// perf.cpp tests — the read-modify-write ops on androws.<pkg>.local_user_config.
//
// The contract that matters: writing one flag must NOT drop the other dozen
// keys GameLoop keeps in that document (resolution arrays, toolbar state, the
// duplicated fps fields). A replace-the-whole-blob bug would quietly reset the
// user's display settings.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/perf.hpp"

#include <nlohmann/json.hpp>

#include <string>

using namespace glt::perf;
using json = nlohmann::json;

namespace {

// A realistic document, as verified against a live Configs.db.
// NOTE: one raw-string literal. Splitting it with an embedded )R"(" terminates
// the first literal early and silently truncates the JSON.
constexpr const char* kRealConfig =
    R"({"app_close_action":{"close_action":0,"is_popup_inquire":true},"aspect_ratio":100,)"
    R"("clarity":2,"frame_rate":120,"hide_full_screen_topbar":true,)"
    R"("is_toolbar_open":false,"landscape_render_wnd_size":[916,515],)"
    R"("monitor_size":[1280,976],"user_select_clarity":true})";

json parse_or_empty(const std::string& s) {
    try {
        return json::parse(s);
    } catch (...) {
        return json::object();
    }
}

} // namespace

// ── Ops ──────────────────────────────────────────────────────────────────────

GLT_TEST(PerfOps, set_clarity_preserves_every_other_key) {
    const auto op = set_clarity_op("com.example", kRealConfig, 2);

    CHECK_EQ(op.sql,
             std::string("UPDATE configs SET value=? WHERE key=?"));
    CHECK_EQ(op.params.size(), std::size_t{2});
    CHECK_EQ(op.params[1].as_text(), std::string("androws.com.example.local_user_config"));

    const auto after = parse_or_empty(op.params[0].as_text());
    CHECK_EQ(after.at("clarity").get<int>(), 2);
    CHECK_EQ(after.at("frame_rate").get<int>(), 120);
    CHECK_EQ(after.at("aspect_ratio").get<int>(), 100);
    CHECK_EQ(after.at("user_select_clarity").get<bool>(), true);
    CHECK(after.contains("monitor_size"));
    CHECK(after.contains("landscape_render_wnd_size"));
    CHECK_EQ(after.at("monitor_size").size(), std::size_t{2});
}

GLT_TEST(PerfOps, set_clarity_records_the_previous_value) {
    const auto op = set_clarity_op("com.example", kRealConfig, 3);
    // The description is what the Logs page shows, so it must name the value
    // that was there before.
    CHECK(op.describe.find("2") != std::string::npos);
    CHECK(op.describe.find("-> 3") != std::string::npos);
}

GLT_TEST(PerfOps, set_flag_preserves_other_keys) {
    const auto op = set_flag_op("com.example", kRealConfig, kLowLatency, true);

    const auto after = parse_or_empty(op.params[0].as_text());
    CHECK_EQ(after.at("low_latency_mode").get<bool>(), true);
    CHECK_EQ(after.at("clarity").get<int>(), 2);
    CHECK_EQ(after.at("frame_rate").get<int>(), 120);
}

GLT_TEST(PerfOps, frame_gen_is_set_to_false) {
    // "Fake 240" is worse than real 120: input lag and artefacts. The fix forces
    // the value OFF, and it must be a real JSON false, not a string "false".
    const auto op = set_flag_op("com.example", kRealConfig, kFrameGen, kFrameGenValue);
    const auto after = parse_or_empty(op.params[0].as_text());

    CHECK_EQ(after.at("default_frame_gen_on").get<bool>(), false);
    CHECK(after.at("default_frame_gen_on").is_boolean());
}

GLT_TEST(PerfOps, creates_the_document_when_it_is_absent) {
    // The key may not exist at all on a fresh install. The op must still be
    // well-formed rather than producing "null".
    const auto op = set_flag_op("com.example", "", kLowLatency, true);
    const auto after = parse_or_empty(op.params[0].as_text());

    CHECK(after.contains("low_latency_mode"));
    CHECK_EQ(op.describe.find("unset") != std::string::npos, true);
}

GLT_TEST(PerfOps, survives_a_corrupt_document) {
    // A truncated JSON blob must not abort an Apply. It degrades to a fresh
    // object, and the operator can see what was lost in the written file.
    const auto op = set_flag_op("com.example", "{not json", kLowLatency, true);
    const auto after = parse_or_empty(op.params[0].as_text());
    CHECK_EQ(after.at("low_latency_mode").get<bool>(), true);
}

GLT_TEST(PerfOps, writes_compact_json_not_indented) {
    // Python used json.dumps(separators=(',',':')). An indented blob bloats
    // the row and is harder to diff against a known-good copy.
    const auto op = set_flag_op("com.example", kRealConfig, kLowLatency, true);
    const std::string blob = op.params[0].as_text();

    CHECK(blob.find('\n') == std::string::npos);
    CHECK(blob.find(", ") == std::string::npos); // no space after a comma
}

// ── Explanations ─────────────────────────────────────────────────────────────

GLT_TEST(PerfExplains, every_flag_we_write_is_documented) {
    // An undocumented flag means nobody knows why the value was chosen.
    CHECK(!explain(kLowLatency).empty());
    CHECK(!explain(kFrameGen).empty());
    CHECK(!explain(kClarity).empty());
}

GLT_TEST(PerfExplains, unknown_key_returns_empty) {
    CHECK_EQ(explain("no_such_flag"), std::string(""));
}

// ── Constants ────────────────────────────────────────────────────────────────

GLT_TEST(PerfConstants, tier_and_flag_values_match_the_spec) {
    CHECK_EQ(static_cast<int>(Clarity::Low), 1);
    CHECK_EQ(static_cast<int>(Clarity::Sweet), 2);
    CHECK_EQ(static_cast<int>(Clarity::High), 3);

    CHECK_EQ(kLowLatencyValue, true);
    CHECK_EQ(kFrameGenValue, false);

    // The key names are GameLoop's own. Renaming one silently writes a field
    // the engine never reads.
    CHECK_EQ(std::string(kLowLatency), std::string("low_latency_mode"));
    CHECK_EQ(std::string(kFrameGen), std::string("default_frame_gen_on"));
    CHECK_EQ(std::string(kClarity), std::string("clarity"));
}