// ─────────────────────────────────────────────────────────────────────────────
// registry / paths / procs constant tests.
//
// These modules touch the live machine, so only their CONSTANTS are asserted
// here — and the constants are exactly where a divergence is silent. Writing
// PerformanceCpuCoreNum to a key the engine never reads produces a successful
// registry call, a green log line, and no change at all.
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/paths.hpp"
#include "glt/core/procs.hpp"
#include "glt/core/registry.hpp"
#include "glt/core/strings.hpp"

#include <string>

using namespace glt;

// ── Registry ─────────────────────────────────────────────────────────────────

GLT_TEST(RegistryKeys, live_under_settings_common) {
    // Verified on this machine: the values are NOT at ...\GameLoop directly,
    // and there is no ...\GameLoop\Performance key at all. An earlier draft of
    // the port spec named one, and writing there would have been a no-op.
    CHECK_EQ(std::wstring(reg::kBase), std::wstring(L"Software\\Tencent\\GameLoop"));
    CHECK_EQ(std::wstring(reg::kCommon),
             std::wstring(L"Software\\Tencent\\GameLoop\\Settings\\Common"));

    // kCommon must be kBase plus the Settings\Common suffix, or the two
    // constants can drift apart and only one of them would be true.
    CHECK_EQ(std::wstring(reg::kCommon), std::wstring(reg::kBase) + L"\\Settings\\Common");
}

GLT_TEST(RegistryKeys, value_names_match_the_engine) {
    CHECK_EQ(reg::kPerfCores, std::wstring_view(L"PerformanceCpuCoreNum"));
    CHECK_EQ(reg::kPerfMemGb, std::wstring_view(L"PerformanceMemoryGb"));
    CHECK_EQ(reg::kPerfMode, std::wstring_view(L"PerformanceMode"));
    CHECK_EQ(reg::kRenderMode, std::wstring_view(L"RenderMode"));
    CHECK_EQ(reg::kRenderUserType, std::wstring_view(L"RenderUserType"));
}

GLT_TEST(RegistryLabels, render_mode_enum) {
    // Undocumented numeric enum; the labels are what the user reads.
    CHECK_EQ(reg::render_mode_label(std::int64_t{0}),
             std::wstring(L"Auto (engine picks per game)"));
    CHECK_EQ(reg::render_mode_label(std::int64_t{1}), std::wstring(L"OpenGL+"));
    CHECK_EQ(reg::render_mode_label(std::int64_t{2}), std::wstring(L"DirectX+"));
    CHECK_EQ(reg::render_mode_label(std::int64_t{3}), std::wstring(L"Vulkan"));
    CHECK_EQ(reg::render_mode_label(std::int64_t{9}),
             std::wstring(L"unknown (9)"));
}

GLT_TEST(RegistryLabels, render_user_type_enum) {
    CHECK_EQ(reg::render_user_label(std::int64_t{0}),
             std::wstring(L"Auto (never overridden)"));
    CHECK_EQ(reg::render_user_label(std::int64_t{1}),
             std::wstring(L"Manually overridden"));
}

GLT_TEST(RegistryLabels, unset_is_distinguishable_from_zero) {
    // RenderMode "unset" must not render as "0 - Auto", which would claim the
    // engine is on auto-selection when in fact GameLoop has not written the
    // value at all.
    CHECK_EQ(reg::render_mode_label(std::nullopt), std::wstring(L"unset"));
    CHECK_EQ(reg::render_user_label(std::nullopt), std::wstring(L"unset"));
}

// ── Paths ────────────────────────────────────────────────────────────────────

GLT_TEST(PathConstants, package_is_the_codm_mobile_one) {
    // kPkg is UTF-8 (a std::string_view), because every consumer of it builds a
    // Configs.db KEY and the whole key/value layer works in UTF-8.
    CHECK_EQ(paths::kPkg,
             std::string_view("com.activision.callofduty.shooter"));
    CHECK(paths::kPkg.find("activision") != std::string_view::npos);
}

GLT_TEST(PathConstants, discovery_finds_the_live_install) {
    // On a machine with GameLoop installed this must succeed. The port's whole
    // portability claim rests on there being no hardcoded drive letter or user.
    const std::wstring root = paths::install_root();
    const std::wstring db = paths::appdata_db();

    // Not asserting non-empty: the test must also pass on a machine without
    // GameLoop. Asserting only that they do not throw.
    if (!root.empty()) {
        CHECK(str::dir_exists(root));
        // The build directory must be a "*.*.*" folder, not the Application
        // root (which holds only launcher stubs).
        const std::wstring build = paths::app_version_dir(root);
        CHECK(!build.empty());
        CHECK(build.rfind(root, 0) == 0);
    }
    if (!db.empty()) {
        CHECK(str::file_exists(db));
        CHECK(db.find(L"Configs.db") != std::wstring::npos);
    }
}

// ── Processes ────────────────────────────────────────────────────────────────

GLT_TEST(ProcIdentity, recognises_gameloop_images) {
    CHECK(procs::is_gameloop_image(L"GameLoop.exe"));
    CHECK(procs::is_gameloop_image(L"GameLoopVm.exe"));
    CHECK(procs::is_gameloop_image(L"GameLoopEmulator.exe"));
    CHECK(procs::is_gameloop_image(L"GameLoopDldSvr.exe"));
    // Case-insensitive: the Python filter lowercased first.
    CHECK(procs::is_gameloop_image(L"gameloopvm.exe"));
    CHECK(procs::is_gameloop_image(L"cefrendererprocess.exe"));
    CHECK(procs::is_gameloop_image(L"AppMarket.exe"));
    CHECK(procs::is_gameloop_image(L"androidprocess.exe"));
    CHECK(procs::is_gameloop_image(L"aow_exe.exe"));
}

GLT_TEST(ProcIdentity, rejects_everything_else) {
    CHECK(!procs::is_gameloop_image(L"explorer.exe"));
    CHECK(!procs::is_gameloop_image(L"chrome.exe"));
    // The tuner itself must never appear, or Close GameLoop kills the app.
    CHECK(!procs::is_gameloop_image(L"GameLoopTuner.exe"));
    CHECK(!procs::is_gameloop_image(L"GL Tuner.exe"));
}

GLT_TEST(ProcConstants, service_name) {
    CHECK_EQ(std::wstring(procs::kService), std::wstring(L"GameLoopService"));
    CHECK_EQ(std::wstring(procs::kServiceExe), std::wstring(L"gameloopservice.exe"));
}

GLT_TEST(ProcPriority, default_plan) {
    const auto plan = procs::default_priority_plan();

    bool emulator_high = false, vm_high = false, assistant_above = false, dl_below = false;
    for (const auto& [img, prio] : plan) {
        if (img == L"gameloopemulator.exe") emulator_high = (prio == procs::kPrioHigh);
        if (img == L"gameloopvm.exe")           vm_high = (prio == procs::kPrioHigh);
        if (img == L"gameloopassistant.exe")    assistant_above = (prio == procs::kPrioAbove);
        if (img == L"gameloopdldsvr.exe")       dl_below = (prio == procs::kPrioBelow);
    }
    CHECK(emulator_high);
    CHECK(vm_high);
    CHECK(assistant_above);
    CHECK(dl_below);
}

GLT_TEST(ProcHost, resources_are_sane) {
    const auto h = procs::host_resources();
    // Every machine this ships to has more than 1 core and more than 1 GB.
    CHECK(h.cores >= 1);
    CHECK(h.mem_gb >= 1);
}

GLT_TEST(ProcService, status_is_queryable_without_throwing) {
    // The service may or may not be installed; either way this must not throw.
    const auto s = procs::service_status();
    if (!s.found) {
        CHECK(s.state_text() == L"NOT FOUND");
    }
}