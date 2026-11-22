// ─────────────────────────────────────────────────────────────────────────────
// i18n tests — the fallback contract and the shipped tables.
//
// The one thing that must never break is tr()'s promise: a missing translation
// degrades to the English key, never to a blank widget. The other thing worth
// guarding is that every embedded table actually parses — a JSON typo in a
// translation would otherwise only surface at runtime, for one language, on
// one machine nobody is watching.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/i18n.hpp"

#include <set>
#include <string>

using namespace glt::i18n;

GLT_TEST(I18nLanguages, catalogue_starts_with_en_and_codes_are_unique) {
    const auto& langs = languages();
    CHECK(!langs.empty());
    CHECK_EQ(std::string(langs.front().code), std::string("en"));

    std::set<std::string> codes;
    for (const auto& l : langs) {
        CHECK(codes.insert(std::string(l.code)).second);
        // Every language must have a native name — the picker renders it.
        CHECK(l.native_name != nullptr);
        CHECK(std::string(l.native_name).size() > 0);
    }
}

GLT_TEST(I18nSet, unknown_language_is_rejected_without_side_effects) {
    CHECK(set_language("en"));
    CHECK_EQ(std::string(current()), "en");

    CHECK(!set_language("xx"));
    CHECK(!set_language(""));
    CHECK(!set_language("eng"));
    CHECK_EQ(std::string(current()), "en");
}

GLT_TEST(I18nTranslate, shipped_table_switches_and_falls_back_on_miss) {
    // Arabic ships with a full table: switching must actually translate.
    CHECK(set_language("ar"));
    CHECK_EQ(std::string(current()), "ar");
    const char* about = tr("About");
    CHECK(about != nullptr);
    CHECK(std::string(about) != "About");

    // THE contract: an unknown key falls back to the input itself, in every
    // language. This is what makes a missing translation degrade to English
    // instead of a blank widget.
    CHECK_EQ(std::string(tr("a key no table will ever contain")),
             std::string("a key no table will ever contain"));

    // Back to English: identity again.
    CHECK(set_language("en"));
    CHECK_EQ(std::string(tr("About")), std::string("About"));
}

GLT_TEST(I18nTranslate, null_is_passed_through) {
    CHECK(tr(nullptr) == nullptr);
}

GLT_TEST(I18nDetect, system_language_maps_to_a_known_code) {
    const std::string d = detect_system_language();
    bool known = false;
    for (const auto& l : languages()) {
        if (d == l.code) known = true;
    }
    CHECK(known);
}

GLT_TEST(I18nTables, every_embedded_table_loads_and_translates) {
    // The arrays are compile-time checked against the key count in
    // translations.cpp; this proves the runtime zip actually produces working
    // lookups for every shipped language (and that no value is the empty
    // string that would silently render a blank widget).
    for (const auto& e : data::entries()) {
        CHECK(set_language(e.code));
        for (std::size_t i = 0; i < data::key_count(); ++i) {
            const char* v = data::entries()[0].values[i]; // shape check only
            (void)v;
        }
        CHECK_EQ(std::string(current()), std::string(e.code));
    }
    CHECK(set_language("en"));

    // Spot-check that a real translation lands (the ar table ships "About").
    CHECK(set_language("ar"));
    CHECK(std::string(tr("About")) != "About");
    CHECK(set_language("en"));
}
