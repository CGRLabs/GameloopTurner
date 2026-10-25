// ─────────────────────────────────────────────────────────────────────────────
// Hub fixes tests — blind-skip, hosts default-off, absent tables, registry
// shape (hub/PLAN.md §5, §8, §10; A3.16, A5.1, A3.17).
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/fixes.hpp"
#include "glt/core/fixes_hub.hpp"
#include "glt/core/keymaps.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace glt;

// ── Registry shape (same surface as Beta) ────────────────────────────────────

GLT_TEST(HubFixes, registry_has_every_key_and_one_star) {
    const fixes::Key expected[] = {
        fixes::Key::FullChain, fixes::Key::Pin,     fixes::Key::FlatDmz,
        fixes::Key::FlatBr,    fixes::Key::Blind,   fixes::Key::Scrub,
        fixes::Key::Quarantine, fixes::Key::Unquarantine,
        fixes::Key::HostsBlock, fixes::Key::HostsUnblock,
        fixes::Key::Latency,   fixes::Key::NoFrameGen, fixes::Key::Priority,
    };
    for (fixes::Key k : expected) CHECK(fixes::find(k) != nullptr);
    CHECK_EQ(fixes::registry().size(), std::size(expected));

    int stars = 0;
    for (const auto& d : fixes::registry()) {
        if (d.star) ++stars;
    }
    CHECK_EQ(stars, 1);
    CHECK(fixes::find(fixes::Key::FullChain)->star);
}

GLT_TEST(HubFixes, hosts_block_is_registered_but_not_the_default) {
    // Registered so a user can still opt in...
    CHECK(fixes::find(fixes::Key::HostsBlock) != nullptr);
    // ...but never part of the Hub fullchain.
    CHECK(!fixes::hub_hosts_block_default_enabled());

    for (const auto stage : fixes::hub_fullchain_stages()) {
        if (stage.find("host") != std::string_view::npos) {
            ::glt::test::fail(__FILE__, __LINE__, "hosts stage in Hub fullchain");
        }
    }
}

GLT_TEST(HubFixes, absent_tables_are_documented) {
    const auto tables = fixes::hub_absent_tables();
    bool migrate = false, scenes = false;
    for (const auto t : tables) {
        if (t == "migrate_status_tb") migrate = true;
        if (t == "scenes_config") scenes = true;
    }
    CHECK(migrate);
    CHECK(scenes);
}

// ── Blind tex: skip absent 30004, remove the rest (§5 / A3.16) ───────────────

GLT_TEST(HubFixes, blind_ids_are_the_two_primaries_and_their_twins) {
    const auto ids = fixes::blind_tex_ids();
    CHECK_EQ(ids.size(), std::size_t{4});
    for (const int want : {10004, 10046, 30004, 30046}) {
        CHECK(std::find(ids.begin(), ids.end(), want) != ids.end());
    }
    CHECK(std::is_sorted(ids.begin(), ids.end()));
}

GLT_TEST(HubFixes, blind_skips_the_absent_id_and_succeeds_on_the_rest) {
    // Hub smk.conf ships 10004/10046/30046; 30004 is ABSENT.
    std::string smk =
        "<Tex id=\"10004\" comm=\"lobby\"/>\n"
        "<Tex id=\"10046\" comm=\"skillbox\"/>\n"
        "<Tex id=\"30046\" comm=\"skillbox-twin\"/>\n";

    for (const int id : fixes::blind_tex_ids()) {
        keymaps::TexMatch m;
        const bool removed = keymaps::blind_tex_in_text(smk, id, &m);
        if (id == 30004) {
            // Absent -> false, which the Hub fix reports as "already absent (ok)".
            CHECK(!removed);
        } else {
            CHECK(removed);
        }
    }

    // Everything present was removed; the document is clean of triggers.
    CHECK(keymaps::tex_ids_in_text(smk).empty());
}

GLT_TEST(HubFixes, blind_on_an_already_blinded_document_is_idempotent) {
    std::string smk = "<Tex id=\"1\" comm=\"x\"/>\n";
    for (const int id : fixes::blind_tex_ids()) {
        CHECK(!keymaps::blind_tex_in_text(smk, id, nullptr));
    }
    CHECK_EQ(smk, std::string("<Tex id=\"1\" comm=\"x\"/>\n"));
}

// ── Close requirement (unchanged from Beta) ──────────────────────────────────

GLT_TEST(HubFixes, db_writes_require_game_loop_closed) {
    const std::vector<fixes::Key> must_close = {
        fixes::Key::FullChain, fixes::Key::Pin,    fixes::Key::FlatDmz,
        fixes::Key::FlatBr,    fixes::Key::Blind,  fixes::Key::Scrub,
        fixes::Key::Quarantine, fixes::Key::Unquarantine,
        fixes::Key::Latency,   fixes::Key::NoFrameGen,
    };
    for (fixes::Key k : must_close) CHECK(fixes::needs_gameloop_closed({k}));

    CHECK(!fixes::needs_gameloop_closed({fixes::Key::Priority}));
    CHECK(!fixes::needs_gameloop_closed({fixes::Key::HostsBlock}));
}

// ── detect_applied must not throw with recentlyused absent (§2) ──────────────

GLT_TEST(HubFixes, detect_applied_returns_only_known_keys) {
    for (fixes::Key k : fixes::detect_applied()) CHECK(fixes::find(k) != nullptr);
}