// ─────────────────────────────────────────────────────────────────────────────
// glt/core/fixes_hub.hpp — Hub (TAC) fix behaviour.
//
// The fix registry and the SQL skeleton are shared with Beta, but several fixes
// change behaviour on Hub (docs/targets/hub/PLAN.md §4, §5, §8, §10):
//   * set_latency / set_no_frame_gen SKIP a flag the build does not carry
//     instead of writing (and thus creating) it;
//   * the blind fix skips absent Tex ids (30004) and treats "already absent" as
//     success;
//   * the experimental hosts block is NOT part of the Hub fullchain;
//   * migrate_status_tb / scenes_config must never be referenced.
//
// Declarations here are Hub-only and drive the Hub tests.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string_view>
#include <vector>

namespace glt::fixes {

// Hub fullchain leaves the experimental hosts block OUT (report A5.1: the
// pckeymap host is not referenced by this build at all). The fix stays
// registered for users who want it; it is never auto-applied.
[[nodiscard]] bool hub_hosts_block_default_enabled();

// The Tex ids the Hub blind fix targets. 30004 is ABSENT in Hub; the callers
// skip it and report "already absent" rather than failing.
[[nodiscard]] std::vector<int> hub_blind_tex_ids();

// Table names the Hub schema does NOT have. No Hub code path may query them.
[[nodiscard]] std::vector<std::string_view> hub_absent_tables();

// Ordered stage labels of the Hub fullchain, for a test to assert the hosts
// block is not one of them.
[[nodiscard]] std::vector<std::string_view> hub_fullchain_stages();

} // namespace glt::fixes