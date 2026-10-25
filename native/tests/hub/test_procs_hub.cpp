// ─────────────────────────────────────────────────────────────────────────────
// Hub process tests — new images and kill order (hub/PLAN.md §7).
// ─────────────────────────────────────────────────────────────────────────────
#include "harness.hpp"

#include "glt/core/procs.hpp"
#include "glt/core/procs_hub.hpp"
#include "glt/core/strings.hpp"

#include <algorithm>
#include <string>

using namespace glt;

GLT_TEST(HubProcs, recognises_the_new_images) {
    for (const auto img : procs::hub_extra_game_images()) {
        CHECK(procs::is_gameloop_image(img));
    }
    // Case-insensitive, and by their real spelling.
    CHECK(procs::is_gameloop_image(L"GLABoxHeadless.exe"));
    CHECK(procs::is_gameloop_image(L"GLABoxSVC.exe"));
    CHECK(procs::is_gameloop_image(L"AceKeeper.exe"));
    CHECK(procs::is_gameloop_image(L"AceKeeperService.exe"));
    CHECK(procs::is_gameloop_image(L"GameLoopVfs.exe"));
}

GLT_TEST(HubProcs, keeps_every_core_image) {
    for (const wchar_t* img : {L"GameLoop.exe", L"GameLoopEmulator.exe", L"GameLoopVm.exe",
                               L"GameLoopAssistant.exe", L"GameLoopDldSvr.exe",
                               L"cefrendererprocess.exe"}) {
        CHECK(procs::is_gameloop_image(img));
    }
}

GLT_TEST(HubProcs, still_rejects_non_gameloop) {
    CHECK(!procs::is_gameloop_image(L"explorer.exe"));
    CHECK(!procs::is_gameloop_image(L"chrome.exe"));
    CHECK(!procs::is_gameloop_image(L"GameLoopTuner.exe"));
}

GLT_TEST(HubProcs, kill_order_starts_with_the_setup_json_order) {
    const auto order = procs::hub_kill_order();
    CHECK(order.size() >= 5);

    CHECK_EQ(std::wstring(order[0]), std::wstring(L"glaboxheadless.exe"));
    CHECK_EQ(std::wstring(order[1]), std::wstring(L"glaboxsvc.exe"));
    CHECK_EQ(std::wstring(order[2]), std::wstring(L"acekeeper.exe"));
    CHECK_EQ(std::wstring(order[3]), std::wstring(L"acekeeperservice.exe"));
    CHECK_EQ(std::wstring(order[4]), std::wstring(L"gameloopvfs.exe"));
}

GLT_TEST(HubProcs, kill_order_contains_each_new_image_exactly_once) {
    const auto order = procs::hub_kill_order();
    for (const auto img : procs::hub_extra_game_images()) {
        const auto n = std::count(order.begin(), order.end(), img);
        if (n != 1) {
            ::glt::test::fail(__FILE__, __LINE__,
                              std::string("kill order count != 1 for ") +
                                  str::to_utf8(std::wstring(img)));
        }
    }
}

GLT_TEST(HubProcs, priority_table_includes_the_new_images) {
    const auto plan = procs::default_priority_plan();

    const auto in_plan = [&](std::wstring_view want) {
        for (const auto& [img, _] : plan) {
            if (str::iequals(img, want)) return true;
        }
        return false;
    };

    for (const auto img : procs::hub_extra_game_images()) CHECK(in_plan(img));

    // Core entries are preserved with their Beta priorities.
    const auto prio = [&](std::wstring_view want) -> std::wstring {
        for (const auto& [img, p] : plan) {
            if (str::iequals(img, want)) return p;
        }
        return {};
    };
    CHECK_EQ(prio(L"gameloopemulator.exe"), std::wstring(procs::kPrioHigh));
    CHECK_EQ(prio(L"gameloopvm.exe"), std::wstring(procs::kPrioHigh));
    CHECK_EQ(prio(L"gameloopassistant.exe"), std::wstring(procs::kPrioAbove));
    CHECK_EQ(prio(L"gameloopdldsvr.exe"), std::wstring(procs::kPrioBelow));
}