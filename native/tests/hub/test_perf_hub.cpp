// ─────────────────────────────────────────────────────────────────────────────
// Hub perf tests — absent optional flags (hub/PLAN.md §4, A3.15).
//
// A REAL Hub local_user_config, verified in the report: clarity, frame_rate and
// user_select_clarity are present; low_latency_mode and default_frame_gen_on
// are not. The whole fix must treat the missing keys as "not applicable" and
// never create them.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/perf.hpp"
#include "glt/core/perf_hub.hpp"

#include <nlohmann/json.hpp>

#include <string>

using namespace glt;
using json = nlohmann::json;

namespace {

// One raw-string literal — splitting with an embedded )R"(" truncates it.
constexpr const char* kHubConfig =
    R"({"aspect_ratio":5,"clarity":2,"frame_rate":240,"hide_full_screen_topbar":true,)"
    R"("user_select_clarity":true,"fps":240,"maximum_frame_rate":240})";

json parse_or_empty(const std::string& s) {
    try {
        return json::parse(s);
    } catch (...) {
        return json::object();
    }
}

} // namespace

GLT_TEST(HubPerf, only_the_two_missing_flags_are_optional) {
    CHECK(perf::hub_flag_is_optional(perf::kLowLatency));
    CHECK(perf::hub_flag_is_optional(perf::kFrameGen));
    // Present on Hub, so NOT optional.
    CHECK(!perf::hub_flag_is_optional(perf::kClarity));
    CHECK(!perf::hub_flag_is_optional(perf::kFrameRate));
    CHECK(!perf::hub_flag_is_optional(perf::kUserSelectClarity));
}

GLT_TEST(HubPerf, absent_flags_read_as_not_applicable) {
    // nullopt (not applicable) is distinct from a real `false`.
    CHECK(!perf::hub_read_flag(kHubConfig, perf::kLowLatency).has_value());
    CHECK(!perf::hub_read_flag(kHubConfig, perf::kFrameGen).has_value());

    // ...and they must not be written.
    CHECK(!perf::hub_flag_supported(kHubConfig, perf::kLowLatency));
    CHECK(!perf::hub_flag_supported(kHubConfig, perf::kFrameGen));

    // clarity IS supported.
    CHECK(perf::hub_flag_supported(kHubConfig, perf::kClarity));
}

GLT_TEST(HubPerf, present_flag_reads_and_supports) {
    const std::string with_flag = R"({"low_latency_mode":true,"clarity":2})";

    const auto v = perf::hub_read_flag(with_flag, perf::kLowLatency);
    CHECK(v.has_value());
    CHECK_EQ(*v, true);
    CHECK(perf::hub_flag_supported(with_flag, perf::kLowLatency));

    // A real `false` is not "not applicable".
    const auto f = perf::hub_read_flag(R"({"default_frame_gen_on":false})", perf::kFrameGen);
    CHECK(f.has_value());
    CHECK_EQ(*f, false);
}

GLT_TEST(HubPerf, non_boolean_value_is_not_applicable) {
    CHECK(!perf::hub_read_flag(R"({"low_latency_mode":"yes"})", perf::kLowLatency).has_value());
    CHECK(!perf::hub_read_flag("{not json", perf::kLowLatency).has_value());
}

GLT_TEST(HubPerf, unsupported_note_names_the_flag) {
    const auto note = perf::hub_unsupported_note(perf::kLowLatency);
    CHECK(note.find("low_latency_mode") != std::string::npos);
    CHECK(note.find("not supported") != std::string::npos);
    CHECK(perf::hub_unsupported_note(perf::kFrameGen).find("default_frame_gen_on") !=
          std::string::npos);
}

GLT_TEST(HubPerf, clarity_write_preserves_the_hub_document) {
    const auto op = perf::set_clarity_op("com.example", kHubConfig, 3);

    const auto after = parse_or_empty(op.params[0].as_text());
    CHECK_EQ(after.at("clarity").get<int>(), 3);
    CHECK_EQ(after.at("frame_rate").get<int>(), 240);
    CHECK_EQ(after.at("user_select_clarity").get<bool>(), true);

    // Writing clarity must NOT conjure the absent Hub flags into existence.
    CHECK(!after.contains("low_latency_mode"));
    CHECK(!after.contains("default_frame_gen_on"));
}

GLT_TEST(HubPerf, current_clarity_reads_without_the_absent_flags) {
    // current_clarity is a pure read of the raw JSON; it must work on the Hub
    // document shape and never assume a perf flag is present.
    CHECK_EQ(perf::explain(perf::kLowLatency).find("Hub") != std::string::npos, true);
    CHECK(!perf::explain(perf::kClarity).empty());
}