// ─────────────────────────────────────────────────────────────────────────────
// Variant tests — the first-launch selector's model (compiled variant +
// persisted choice). These run under the Hub build, so the compiled variant is
// Hub; the default and the persisted round-trip are variant-independent.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"
#include "hub_test_util.hpp"

#include "glt/core/strings.hpp"
#include "glt/core/variant.hpp"

#include <string>

using namespace glt;

GLT_TEST(VariantHub, compiled_variant_is_hub) {
    // This suite only builds when GLT_TARGET_HUB is defined (CMake gate).
    CHECK(variant::compiled() == variant::Id::Hub);
}

GLT_TEST(Variant, default_is_beta_when_nothing_is_saved) {
    CHECK(variant::default_id() == variant::Id::Beta);
}

GLT_TEST(Variant, labels_and_keys_are_stable) {
    CHECK_EQ(std::string(variant::label(variant::Id::Beta)), std::string("GameLoop Beta"));
    CHECK_EQ(std::string(variant::label(variant::Id::Hub)),
             std::string("GameLoop Hub (TAC)"));
    CHECK_EQ(std::string(variant::key(variant::Id::Beta)), std::string("beta"));
    CHECK_EQ(std::string(variant::key(variant::Id::Hub)), std::string("hub"));
}

GLT_TEST(Variant, parse_is_case_insensitive_and_tolerant) {
    CHECK(variant::parse("beta") == variant::Id::Beta);
    CHECK(variant::parse("HUB") == variant::Id::Hub);
    CHECK(variant::parse("  Hub  ") == variant::Id::Hub);
    CHECK(!variant::parse("").has_value());
    CHECK(!variant::parse("nonsense").has_value());
}

GLT_TEST(Variant, save_load_clear_round_trip) {
    const std::wstring path = hubtest::unique_temp_path(L"glt-variant", L".txt");

    CHECK(variant::save_choice_to(path, variant::Id::Hub));
    {
        const auto got = variant::load_choice_from(path);
        CHECK(got.has_value());
        CHECK(*got == variant::Id::Hub);
    }

    CHECK(variant::save_choice_to(path, variant::Id::Beta));
    {
        const auto got = variant::load_choice_from(path);
        CHECK(got.has_value());
        CHECK(*got == variant::Id::Beta);
    }

    glt::str::remove_file(path);
    CHECK(!variant::load_choice_from(path).has_value());
}

GLT_TEST(Variant, missing_and_garbage_files_fall_back_to_nullopt) {
    CHECK(!variant::load_choice_from(L"").has_value());
    CHECK(!variant::load_choice_from(L"Z:\\no\\such\\glt-variant.txt").has_value());

    const std::wstring path = hubtest::unique_temp_path(L"glt-variant-bad", L".txt");
    CHECK(glt::str::write_file(path, "not-a-variant\n"));
    CHECK(!variant::load_choice_from(path).has_value());
    glt::str::remove_file(path);
}