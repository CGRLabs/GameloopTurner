// ─────────────────────────────────────────────────────────────────────────────
// glt/core/perf_hub.hpp — Hub (TAC) performance-flag handling.
//
// Hub's androws.<pkg>.local_user_config keeps clarity / frame_rate /
// user_select_clarity but NOT low_latency_mode or default_frame_gen_on
// (report A3.15, PLAN §4). The tuner must:
//   * READ those keys defensively and report them as "not applicable" rather
//     than "off" when they are missing;
//   * never CREATE them on a write — the fix reports "not supported on this
//     GameLoop build" instead.
//
// The declarations below are Hub-only and drive the Hub tests.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace glt::perf {

// True for flags that are ABSENT in the Hub build and therefore optional.
[[nodiscard]] bool hub_flag_is_optional(std::string_view key);

// Parses `raw_json` and returns the flag value, or nullopt when the key is
// missing or is not a JSON boolean. nullopt is "not applicable", which the UI
// and the fixes must distinguish from a real `false`.
[[nodiscard]] std::optional<bool> hub_read_flag(std::string_view raw_json,
                                                std::string_view key);

// True when the flag may be written on the Hub build: present, or not one of
// the optional flags. An absent optional flag must not be written.
[[nodiscard]] bool hub_flag_supported(std::string_view raw_json, std::string_view key);

// The note a skipped fix reports. There is exactly one spelling, shared by
// set_latency / set_no_frame_gen, so the two never drift apart.
[[nodiscard]] std::string hub_unsupported_note(std::string_view key);

} // namespace glt::perf