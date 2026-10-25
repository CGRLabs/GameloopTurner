// ─────────────────────────────────────────────────────────────────────────────
// glt/core/keymaps_hub.hpp — Hub (TAC) keymap discovery refinements.
//
// Hub adds a NEW flat Battle-Royale solution, `GameLoopHub.com`
// (sol 1792331375184), and REMOVES the flat multiplayer `145` solution: mode 145
// now holds "Multi Player" (93 rules) and "MP" (0 rules). `keymaps::pick_flat()`
// still discovers by rule count, but for PINNING the Hub build additionally
// restricts the search to the requested MODE, so a 0-rule BR solution can never
// be selected when the user is pinning multiplayer (PLAN §3).
//
// Additive: the bulk of glt::keymaps is reused from keymaps.cpp verbatim (the
// report confirms the shared SQL/key logic is unchanged); only the pinning
// refinement lives in keymaps_hub.cpp.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "glt/core/keymaps.hpp"

namespace glt::keymaps {

// Explicit column lists. Hub's key_map_configs_cache_official gained
// value2..value4 and configs gained id/encrypt/json_data; the tuner must NAME
// the columns it touches and never SELECT *.
inline constexpr std::string_view kHubCacheColumns  = "key, value0, value1";
inline constexpr std::string_view kHubConfigsColumns = "key, value";

// Mode ids the Hub build understands (report A3.13; SAME as Beta).
[[nodiscard]] std::vector<std::string_view> hub_known_mode_ids();

// Flat solution ids the report documents (BR 146, DMZ 1063553) plus the new
// Hub BR solution GameLoopHub.com (1792331375184), which must NOT be preferred
// over the mode-145 pin target.
[[nodiscard]] std::vector<std::string_view> hub_documented_flat_solutions();

// The new Hub-only flat BR solution id, named so a test can assert it is
// accounted for without being hardcoded into the pinning decision.
inline constexpr std::string_view kHubFlatBrSolution = "1792331375184";

// Pins a 0-rule layout FOR A SPECIFIC MODE. Preference order within the mode is
// `prefer` → 'multi' → 'br', then the first flat found. Only when the requested
// mode has no flat layout at all does it fall back to the generic set, which is
// what keeps DMZ/BR flattening working on a machine whose 145 has no flat.
[[nodiscard]] std::optional<Solution> pick_flat_for_mode(const std::vector<Solution>& rows,
                                                         std::string_view mode,
                                                         std::string_view prefer = "mp");

} // namespace glt::keymaps