// ─────────────────────────────────────────────────────────────────────────────
// preflight tests — the pass / warn / fail matrix against fixture databases.
//
// run() itself touches the live machine and is exercised every launch; what
// the tests pin down are the two halves it delegates to: the DB shape matrix
// (healthy / missing table / changed columns / never-launched) and the
// smk.conf matrix (present / blinded / suspicious). A wrong verdict here is
// exactly the failure mode the preflight exists to prevent: a quiet no-op.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/db.hpp"
#include "glt/core/preflight.hpp"
#include "glt/core/strings.hpp"

#include <windows.h>

#include <string>

using namespace glt::preflight;

namespace {

std::wstring scratch_dir() {
    wchar_t dir[MAX_PATH];
    const DWORD n = ::GetTempPathW(MAX_PATH, dir);
    return n > 0 && n < MAX_PATH ? std::wstring(dir, n) : std::wstring(L".");
}

std::wstring db_path(const char* name) {
    return glt::str::path_join(scratch_dir(), glt::str::to_wide(name));
}

// The real schema, as verified against live Configs.db (keymaps.hpp header).
void make_healthy_db(const std::wstring& p) {
    glt::str::remove_file(p);
    glt::db::Connection con(p, 5000);
    con.execute("CREATE TABLE key_map_configs_cache_official "
                "(key TEXT PRIMARY KEY, value0 TEXT, value1 TEXT)");
    con.execute("CREATE TABLE configs (key TEXT PRIMARY KEY, value TEXT)");
    con.execute("INSERT INTO key_map_configs_cache_official (key) VALUES "
                "('keymap_mode.unlogin.com.example.shooter.145')");
}

const Check* find_check(const Report& r, const char* name) {
    for (const auto& c : r.checks) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

// A <Tex> element laid out like a real smk.conf (the matcher is tested
// elsewhere; here it only has to classify present vs absent).
constexpr const char* kTex =
    "<Tex id=\"10004\" width=\"1024\" height=\"1024\" max_bufsize=\"20480\" "
    "c1=\"236855296\" c2=\"202247694\" cut_width=\"40\" cut_height=\"46\" "
    "path=\"225154410\"\n\tcomm=\"mailbox\"/>\n";

} // namespace

// ── DB checks ────────────────────────────────────────────────────────────────

GLT_TEST(PreflightDb, healthy_fixture_is_all_ok) {
    const std::wstring p = db_path("glt_pf_healthy.db");
    make_healthy_db(p);

    glt::db::Connection con(p, 5000);
    const Report r = run_db_checks(con);

    CHECK(r.overall == Status::Ok);
    CHECK(find_check(r, "keymap table present") != nullptr);
    CHECK(find_check(r, "configs table present") != nullptr);
    CHECK(find_check(r, "keymap table columns") != nullptr);
    CHECK(find_check(r, "engine keymap state") != nullptr);
    CHECK_EQ(r.summary_line(), std::string("all checks passed"));

    (void)glt::str::remove_file(p);
}

GLT_TEST(PreflightDb, missing_keymap_table_fails) {
    const std::wstring p = db_path("glt_pf_notable.db");
    glt::str::remove_file(p);
    glt::db::Connection con(p, 5000);
    con.execute("CREATE TABLE configs (key TEXT PRIMARY KEY, value TEXT)");

    const Report r = run_db_checks(con);
    CHECK(r.overall == Status::Fail);
    const Check* c = find_check(r, "keymap table present");
    CHECK(c != nullptr);
    if (c) CHECK(c->status == Status::Fail);
    // The summary names the count: the UI and the report show it verbatim.
    CHECK_EQ(r.summary_line(), std::string("1 check(s) failed"));

    (void)glt::str::remove_file(p);
}

GLT_TEST(PreflightDb, changed_columns_fail) {
    // The schema-change simulation: right table name, wrong column layout.
    // This is what a GameLoop update would look like from the outside.
    const std::wstring p = db_path("glt_pf_badcols.db");
    glt::str::remove_file(p);
    glt::db::Connection con(p, 5000);
    con.execute("CREATE TABLE key_map_configs_cache_official "
                "(k TEXT PRIMARY KEY, v0 TEXT, v1 TEXT)");
    con.execute("CREATE TABLE configs (key TEXT PRIMARY KEY, value TEXT)");
    con.execute("INSERT INTO key_map_configs_cache_official (k) VALUES "
                "('keymap_mode.unlogin.com.example.shooter.145')");

    const Report r = run_db_checks(con);
    CHECK(r.overall == Status::Fail);
    const Check* c = find_check(r, "keymap table columns");
    CHECK(c != nullptr);
    if (c) CHECK(c->status == Status::Fail);

    (void)glt::str::remove_file(p);
}

GLT_TEST(PreflightDb, zero_keymap_rows_warns_but_does_not_fail) {
    // A fresh install that has never launched the game: structure intact,
    // state empty. Applying would find no modes to discover — warn, don't
    // block, because the fix itself reports "nothing to do" honestly.
    const std::wstring p = db_path("glt_pf_empty.db");
    glt::str::remove_file(p);
    glt::db::Connection con(p, 5000);
    con.execute("CREATE TABLE key_map_configs_cache_official "
                "(key TEXT PRIMARY KEY, value0 TEXT, value1 TEXT)");
    con.execute("CREATE TABLE configs (key TEXT PRIMARY KEY, value TEXT)");

    const Report r = run_db_checks(con);
    CHECK(r.overall == Status::Warn);
    const Check* c = find_check(r, "engine keymap state");
    CHECK(c != nullptr);
    if (c) CHECK(c->status == Status::Warn);
    CHECK_EQ(r.summary_line(), std::string("1 warning(s)"));

    (void)glt::str::remove_file(p);
}

// ── smk.conf checks ──────────────────────────────────────────────────────────

GLT_TEST(PreflightSmk, missing_path_warns) {
    const auto checks = run_smk_checks(L"");
    CHECK_EQ(checks.size(), std::size_t{1});
    CHECK(checks[0].status == Status::Warn);
}

GLT_TEST(PreflightSmk, templates_present_is_ok) {
    const std::wstring p = db_path("glt_pf_smk_live.conf");
    CHECK(glt::str::write_file(p, std::string("<root>") + kTex + "</root>"));

    const auto checks = run_smk_checks(p);
    CHECK_EQ(checks.size(), std::size_t{3});
    for (const auto& c : checks) CHECK(c.status == Status::Ok);

    (void)glt::str::remove_file(p);
}

GLT_TEST(PreflightSmk, blinded_and_readonly_is_ok_with_detail) {
    // The post-quarantine state: no templates + the read-only lock. This must
    // read as healthy, not as "format changed" — the lock is the tiebreaker.
    const std::wstring p = db_path("glt_pf_smk_blinded.conf");
    CHECK(glt::str::write_file(p, "<root></root>"));
    CHECK(glt::str::set_readonly(p, true));

    const auto checks = run_smk_checks(p);
    CHECK_EQ(checks.size(), std::size_t{3});
    for (const auto& c : checks) CHECK(c.status == Status::Ok);

    glt::str::set_readonly(p, false);
    (void)glt::str::remove_file(p);
}

GLT_TEST(PreflightSmk, empty_and_unlocked_warns) {
    // No templates AND no lock: ambiguous. Could be already-blinded (lock
    // cleared by hand) or a changed format. Warn, because Apply All will
    // report exactly what it finds either way.
    const std::wstring p = db_path("glt_pf_smk_empty.conf");
    CHECK(glt::str::write_file(p, "<root></root>"));

    const auto checks = run_smk_checks(p);
    CHECK_EQ(checks.size(), std::size_t{3});
    const Check& last = checks.back();
    CHECK_EQ(last.name, std::string("smk.conf templates"));
    CHECK(last.status == Status::Warn);

    (void)glt::str::remove_file(p);
}
