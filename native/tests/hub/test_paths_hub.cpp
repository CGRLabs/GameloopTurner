// ─────────────────────────────────────────────────────────────────────────────
// Hub paths tests — install-root discovery (hub/PLAN.md §1, §9).
//
// The report's machine installs to C:\ARENA\GameLoop, which the fixed-drive scan
// does not reach, and the WOW6432Node mirror carries no install value. Those two
// facts are what these tests pin down: they assert the DISCOVERY CONTRACT, not
// the machine on the report (which no CI runner has).
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/paths.hpp"
#include "glt/core/paths_hub.hpp"
#include "glt/core/strings.hpp"

#include <algorithm>
#include <string>

using namespace glt;

// ── Registry branch (§9) ─────────────────────────────────────────────────────

GLT_TEST(HubPaths, registry_branch_is_native_64_only) {
    const auto subs = paths::hub_registry_subkeys();
    CHECK_EQ(subs.size(), std::size_t{1});
    CHECK_EQ(std::wstring(subs[0]), std::wstring(paths::kHubRegistryKey));

    // The WOW6432Node mirror must NOT be a discovery source: it has no install
    // value on Hub, so depending on it would fail on every Hub machine.
    for (const auto s : subs) {
        CHECK(std::wstring(s) != std::wstring(paths::kHubWow64RegistryKey));
    }
}

GLT_TEST(HubPaths, installpath_is_the_first_value_tried) {
    const auto names = paths::hub_registry_value_names();
    CHECK(!names.empty());
    CHECK_EQ(std::wstring(names.front()), std::wstring(L"InstallPath"));
}

// ── Drive scan stays as the fallback (§1) ────────────────────────────────────

GLT_TEST(HubPaths, drive_scan_fallback_is_kept) {
    const auto tails = paths::hub_drive_scan_tails();
    CHECK(!tails.empty());
    CHECK(std::find(tails.begin(), tails.end(),
                    std::wstring_view(L"Program Files\\Tencent\\GameLoop")) != tails.end());
    CHECK(std::find(tails.begin(), tails.end(), std::wstring_view(L"Tencent\\GameLoop")) !=
          tails.end());
}

// ── Launcher drops AppMarket (§1 / A1.7) ─────────────────────────────────────

GLT_TEST(HubPaths, launcher_names_drop_appmarket) {
    const auto names = paths::hub_launcher_names();

    bool has_launcher = false, has_gameloop = false, has_market = false;
    for (const auto n : names) {
        const std::wstring s(n);
        if (s == L"GameLoopLauncher.exe") has_launcher = true;
        if (s == L"GameLoop.exe") has_gameloop = true;
        if (s == L"AppMarket.exe") has_market = true;
    }
    CHECK(has_launcher);
    CHECK(has_gameloop);
    CHECK(!has_market);
}

// ── Live behaviour must not throw on a machine without Hub ───────────────────

GLT_TEST(HubPaths, install_root_and_derivatives_do_not_throw) {
    // On the report's machine this resolves to C:\ARENA\GameLoop; on CI it is
    // simply empty. Either way it must not throw and must not invent a path.
    const std::wstring root = paths::install_root();
    if (!root.empty()) CHECK(str::dir_exists(root));

    const std::wstring build = paths::app_version_dir(root);
    if (!build.empty()) CHECK(build.rfind(root, 0) == 0);

    // quarantine dir is a path, present or not (ano_tmp_quarantine is ABSENT in
    // Hub and is created on demand).
    (void)paths::ano_tmp_quarantine_dir();
    (void)paths::launcher_exe(root);
}