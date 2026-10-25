// ─────────────────────────────────────────────────────────────────────────────
// glt/core/procs_hub.hpp — Hub (TAC) process identity and ordering.
//
// Hub ships five images Beta does not (docs/targets/hub/PLAN.md §7):
//   GLABoxHeadless.exe, GLABoxSVC.exe, AceKeeper.exe, AceKeeperService.exe,
//   GameLoopVfs.exe
// They must be recognised as GameLoop processes and appear in the kill order.
// The kill ORDER is unverified live (report A4.5) so it is taken from the
// install's own Setup.json kill list as a starting point.
//
// Declarations here are Hub-only and used by the Hub tests; the Beta build does
// not include this header.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace glt::procs {

// Images added to is_gameloop_image() for Hub, beyond the shared `gameloop*`
// prefix rule. Lowercase, with the .exe suffix.
[[nodiscard]] std::vector<std::wstring_view> hub_extra_game_images();

// The Hub kill order, as a flat list (game first, UI last). Starts with the
// Setup.json new-image order and keeps the existing core images appended.
[[nodiscard]] std::vector<std::wstring_view> hub_kill_order();

// image name (lowercase, with .exe) → priority, for the Hub build. Superset of
// the Beta plan: the five new images are included.
[[nodiscard]] std::vector<std::pair<std::wstring, std::wstring>> hub_priority_plan();

} // namespace glt::procs