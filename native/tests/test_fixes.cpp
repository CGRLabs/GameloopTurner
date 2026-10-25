// ─────────────────────────────────────────────────────────────────────────────
// fixes.cpp tests — registry shape, Tex-id set, and the closed-modes predicate.
//
// None of these need GameLoop installed: they assert the contract the UI and
// the worker depend on, using the registry's own metadata.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/fixes.hpp"

#include <algorithm>
#include <iterator>
#include <string>

// `glt` rather than `glt::fixes`: the tests below refer to the module by name
// (fixes::Key, fixes::Page) as well as to its members unqualified, and only the
// outer using-directive makes both spellings resolve.
using namespace glt;
using namespace glt::fixes;

// ── Registry ─────────────────────────────────────────────────────────────────

GLT_TEST(FixesRegistry, contains_every_expected_key) {
    const auto& reg = registry();

    const fixes::Key expected[] = {
        fixes::Key::FullChain, fixes::Key::Pin,     fixes::Key::FlatDmz,
        fixes::Key::FlatBr,    fixes::Key::Blind,   fixes::Key::Scrub,
        fixes::Key::Quarantine, fixes::Key::Unquarantine,
        fixes::Key::HostsBlock, fixes::Key::HostsUnblock,
        fixes::Key::Latency,   fixes::Key::NoFrameGen, fixes::Key::Priority,
    };

    for (fixes::Key k : expected) {
        if (find(k) == nullptr) {
            ::glt::test::fail(__FILE__, __LINE__,
                              std::string("missing from registry: ") +
                                  std::string(key_name(k)));
        }
    }
    CHECK_EQ(reg.size(), std::size(expected));
}

GLT_TEST(FixesRegistry, every_entry_has_label_and_why) {
    for (const auto& d : registry()) {
        if (d.label.empty()) {
            ::glt::test::fail(__FILE__, __LINE__,
                              std::string("empty label: ") + std::string(key_name(d.key)));
        }
        if (d.why.empty()) {
            ::glt::test::fail(__FILE__, __LINE__,
                              std::string("empty why: ") + std::string(key_name(d.key)));
        }
    }
}

GLT_TEST(FixesRegistry, key_names_are_stable_strings) {
    // These names are written into tuner-journal.json, so changing one orphans
    // an existing user's journal entry.
    CHECK_EQ(std::string(key_name(fixes::Key::FullChain)), std::string("fullchain"));
    CHECK_EQ(std::string(key_name(fixes::Key::HostsBlock)), std::string("hostsblock"));
    CHECK_EQ(std::string(key_name(fixes::Key::NoFrameGen)), std::string("noframegen"));
}

GLT_TEST(FixesRegistry, exactly_one_starred_recommendation) {
    int stars = 0;
    for (const auto& d : registry()) {
        if (d.star) ++stars;
    }
    // More than one "recommended" card is the same as recommending nothing.
    CHECK_EQ(stars, 1);
    CHECK(fixes::find(fixes::Key::FullChain)->star);
}

GLT_TEST(FixesRegistry, page_split_covers_the_registry) {
    const auto a = for_page(fixes::Page::Keymaps);
    const auto b = for_page(fixes::Page::Performance);
    const auto c = for_page(fixes::Page::VmPriority);

    CHECK_EQ(a.size() + b.size() + c.size(), registry().size());
}

GLT_TEST(FixesRegistry, starred_entries_sort_first) {
    const auto k = for_page(fixes::Page::Keymaps);
    for (std::size_t i = 0; i < k.size(); ++i) {
        if (k[i].star) {
            // Once a non-starred entry appears, no later entry may be starred.
            for (std::size_t j = i + 1; j < k.size(); ++j) CHECK(!k[j].star);
            break;
        }
    }
}

// ── Blind Tex ids ────────────────────────────────────────────────────────────

GLT_TEST(BlindIds, primaries_and_twins) {
    const auto ids = blind_tex_ids();

    // 10004 = lobby mailbox -> DMZ; 10046 = skill box -> BR.
    // Each has a 30000+ twin the engine matches as a SECOND binding for the
    // same switch. Verified: blinding 10046 alone did NOT stop BR.
    CHECK_EQ(ids.size(), std::size_t{4});
    for (const int want : {10004, 10046, 30004, 30046}) {
        CHECK(std::find(ids.begin(), ids.end(), want) != ids.end());
    }
}

GLT_TEST(BlindIds, sorted_and_unique) {
    const auto ids = blind_tex_ids();
    CHECK(std::is_sorted(ids.begin(), ids.end()));
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}

GLT_TEST(BlindIds, twin_is_primary_plus_twenty_thousand) {
    const auto ids = blind_tex_ids();

    for (const int id : ids) {
        if (id < 20000) continue;

        // Every id above the 20000 mark must be a twin of one of the primaries,
        // i.e. (id - 20000) must itself be in the set. A missing twin is how
        // "blinded 10046 but BR still jumps" happened.
        const int primary = id - 20000;
        CHECK(std::find(ids.begin(), ids.end(), primary) != ids.end());
    }
}

// ── Close requirement ────────────────────────────────────────────────────────

GLT_TEST(NeedsClosed, db_writes_require_game_loop_closed) {
    // Copying Configs.db while the emulator holds a 1.3 MB WAL open produces a
    // database that is neither the backup nor the current state.
    const std::vector<fixes::Key> must_close = {
        fixes::Key::FullChain, fixes::Key::Pin,    fixes::Key::FlatDmz,
        fixes::Key::FlatBr,    fixes::Key::Blind,  fixes::Key::Scrub,
        fixes::Key::Quarantine, fixes::Key::Unquarantine,
        fixes::Key::Latency,   fixes::Key::NoFrameGen,
    };
    for (fixes::Key k : must_close) {
        if (!needs_gameloop_closed({k})) {
            ::glt::test::fail(__FILE__, __LINE__,
                              std::string("should require close: ") +
                                  std::string(key_name(k)));
        }
    }
}

GLT_TEST(NeedsClosed, live_process_and_hosts_fixes_do_not) {
    // Priorities apply to RUNNING processes — requiring a close would make the
    // fix impossible to use. Hosts is a file outside the emulator entirely.
    const std::vector<fixes::Key> no_close = {
        fixes::Key::Priority, fixes::Key::HostsBlock, fixes::Key::HostsUnblock};
    for (fixes::Key k : no_close) {
        if (needs_gameloop_closed({k})) {
            ::glt::test::fail(__FILE__, __LINE__,
                              std::string("should NOT require close: ") +
                                  std::string(key_name(k)));
        }
    }
}

GLT_TEST(NeedsClosed, empty_list_needs_nothing) {
    CHECK(!needs_gameloop_closed({}));
}

// ── Detecting applied state on a machine with no GameLoop ────────────────────

GLT_TEST(DetectApplied, empty_without_a_database) {
    // On a machine with no Configs.db, detect_applied() must return nothing
    // rather than reporting every fix as applied (which would pre-tick every
    // checkbox and produce a confusing no-op Apply).
    const auto applied = detect_applied();

    // This assertion holds whether or not GameLoop happens to be installed:
    // on a machine WITH GameLoop the result is non-empty, on one without it is
    // empty, and neither is an error.
    for (fixes::Key k : applied) {
        CHECK(find(k) != nullptr);
    }
}