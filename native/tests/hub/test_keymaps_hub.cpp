// ─────────────────────────────────────────────────────────────────────────────
// Hub keymap tests — explicit columns, absent recentlyused, and the pinning
// refinement (hub/PLAN.md §2, §3, A3.1, A3.2, A3.9, A3.14).
//
// The DB tests build a throwaway SQLite file with the HUB column layout
// (value0..value4, id/encrypt/json_data) using db::Connection's CREATE path, so
// they prove the reads survive the extra columns without needing a real install.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"
#include "hub_test_util.hpp"

#include "glt/core/db.hpp"
#include "glt/core/keymaps.hpp"
#include "glt/core/keymaps_hub.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/perf.hpp"
#include "glt/core/strings.hpp"

#include <string>
#include <vector>

using namespace glt;

namespace {

// Creates the Hub schema in a fresh temp DB and returns its path.
std::wstring make_hub_db() {
    const std::wstring path = hubtest::unique_temp_path(L"glt-hub-km", L".db");
    db::Connection con(path);
    con.execute(
        "CREATE TABLE key_map_configs_cache_official ("
        "id INTEGER PRIMARY KEY, key varchar(256), value0 TEXT, value1 TEXT, "
        "value2 TEXT, value3 TEXT, value4 TEXT)");
    con.execute(
        "CREATE TABLE configs (id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "key varchar(256) UNIQUE, value TEXT, encrypt INT, json_data TEXT)");
    return path;
}

} // namespace

// ── Explicit columns (§2) ────────────────────────────────────────────────────

GLT_TEST(HubKeymaps, column_lists_never_select_star) {
    CHECK(keymaps::kHubCacheColumns.find('*') == std::string_view::npos);
    CHECK(keymaps::kHubConfigsColumns.find('*') == std::string_view::npos);
    CHECK(keymaps::kHubCacheColumns.find("value0") != std::string_view::npos);
    CHECK(keymaps::kHubConfigsColumns.find("value") != std::string_view::npos);
}

// ── Documented ids (§3) ──────────────────────────────────────────────────────

GLT_TEST(HubKeymaps, mode_ids_are_the_documented_constants) {
    const auto modes = keymaps::hub_known_mode_ids();
    for (const auto want : {"145", "146", "531686"}) {
        bool found = false;
        for (const auto m : modes) {
            if (m == want) found = true;
        }
        CHECK(found);
    }
}

GLT_TEST(HubKeymaps, flat_solutions_include_the_new_hub_br_solution) {
    const auto sols = keymaps::hub_documented_flat_solutions();
    bool has_br = false, has_dmz = false, has_new = false;
    for (const auto s : sols) {
        if (s == "146") has_br = true;
        if (s == "1063553") has_dmz = true;
        if (s == keymaps::kHubFlatBrSolution) has_new = true;
    }
    CHECK(has_br);
    CHECK(has_dmz);
    CHECK(has_new);
    CHECK_EQ(std::string(keymaps::kHubFlatBrSolution), std::string("1792331375184"));
}

// ── Pinning refinement (§3) ──────────────────────────────────────────────────

GLT_TEST(HubKeymaps, pin_prefers_the_mode_145_flat_not_the_new_br_flat) {
    // Synthetic rows mirroring the report's Hub DB.
    const std::vector<keymaps::Solution> rows = {
        {"145", "1768790151264", "Multi Player", 93},
        {"145", "1792331370461", "MP", 0},
        {"146", "146", "Battle Royale", 234},
        {"146", "1792331375184", "GameLoopHub.com", 0},
        {"531686", "1063553", "DMZ", 61},
    };

    const auto pick = keymaps::pick_flat_for_mode(rows, "145", "mp");
    CHECK(pick.has_value());
    CHECK_EQ(pick->mode, std::string("145"));
    CHECK_EQ(pick->sol, std::string("1792331370461"));
    CHECK_EQ(pick->name, std::string("MP"));
    CHECK_EQ(pick->rule_count, 0);
}

GLT_TEST(HubKeymaps, generic_pick_flat_still_finds_a_zero_rule_layout) {
    const std::vector<keymaps::Solution> rows = {
        {"145", "1768790151264", "Multi Player", 93},
        {"145", "1792331370461", "MP", 0},
        {"146", "1792331375184", "GameLoopHub.com", 0},
    };
    const auto pick = keymaps::pick_flat(rows, "mp");
    CHECK(pick.has_value());
    CHECK_EQ(pick->rule_count, 0);
}

GLT_TEST(HubKeymaps, falls_back_to_any_flat_only_when_the_mode_has_none) {
    std::vector<keymaps::Solution> rows = {{"531686", "1063553", "DMZ", 61}};

    // No flat anywhere -> no pin.
    CHECK(!keymaps::pick_flat_for_mode(rows, "531686", "mp").has_value());

    // A flat in a different mode is a valid last resort.
    rows.push_back({"146", "1792331375184", "GameLoopHub.com", 0});
    const auto pick = keymaps::pick_flat_for_mode(rows, "531686", "mp");
    CHECK(pick.has_value());
    CHECK_EQ(pick->mode, std::string("146"));
}

// ── DB: absent recentlyused, extra columns tolerated (§2, A3.1, A3.2, A3.9) ──

GLT_TEST(HubKeymaps, absent_recentlyused_reads_as_nullopt) {
    const std::wstring path = make_hub_db();
    {
        db::Connection con(path);

        // One mode row, nothing else. No recentlyused_mode.
        std::vector<db::Value> p;
        p.emplace_back("keymap_mode.unlogin." + std::string(paths::kPkg) + ".145");
        p.emplace_back("{}");
        con.execute(
            "INSERT INTO key_map_configs_cache_official (key, value0, value1) VALUES (?, ?, ?)",
            {p[0], p[1], db::Value(std::string(""))});

        // The absent row must be nullopt, never an auto-created row.
        CHECK(!keymaps::read_recentlyused(con, paths::kPkg).has_value());
        CHECK(!keymaps::read_lastmodeid(con, paths::kPkg).has_value());

        // The mode row still reads through the extra value2..value4 columns.
        const auto modes = keymaps::modes(con, paths::kPkg);
        CHECK_EQ(modes.size(), std::size_t{1});
        CHECK_EQ(modes[0].mode, std::string("145"));
    }
    hubtest::remove_db(path);
}

GLT_TEST(HubKeymaps, writes_do_not_create_recentlyused) {
    const std::wstring path = make_hub_db();
    {
        db::Connection con(path);

        // scrub_recent_op builds an UPDATE, which is a no-op on an absent row.
        const auto op = keymaps::scrub_recent_op(paths::kPkg, 145);
        const int changed = con.execute(op.sql, op.params);
        CHECK_EQ(changed, 0); // nothing created

        CHECK(!keymaps::read_recentlyused(con, paths::kPkg).has_value());
    }
    hubtest::remove_db(path);
}

GLT_TEST(HubKeymaps, configs_reads_through_id_encrypt_json_data) {
    const std::wstring path = make_hub_db();
    {
        db::Connection con(path);

        std::vector<db::Value> p;
        p.emplace_back("androws." + std::string(paths::kPkg) + ".smk");
        p.emplace_back(std::string("<Tex id=\"1\"/>"));
        p.emplace_back(0);                       // encrypt
        p.emplace_back(std::string("{\"x\":1}")); // json_data
        con.execute("INSERT INTO configs (key, value, encrypt, json_data) VALUES (?, ?, ?, ?)", p);

        // keymaps + perf both read `value` by name and must ignore id/encrypt.
        const auto smk = keymaps::read_smk(con, paths::kPkg);
        CHECK(smk.has_value());
        CHECK_EQ(*smk, std::string("<Tex id=\"1\"/>"));

        CHECK_EQ(perf::local_user_config_raw(con, paths::kPkg), std::string("{}"));
    }
    hubtest::remove_db(path);
}