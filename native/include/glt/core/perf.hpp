// ─────────────────────────────────────────────────────────────────────────────
// glt/core/perf.hpp — androws.<pkg>.local_user_config / host_config.
//
// Ported from legacy-python/src/core/perf.py. The plain-language EXPLAINS are
// carried over verbatim: they are load-bearing documentation that the tuning
// values were validated against a real RX 5700 XT rig, and several of them
// record a negative result (clarity tier 1 measurably performed WORSE than
// tier 3 on this engine) that a future maintainer would otherwise re-litigate.
//
// Stored document (verified live):
//   {"clarity":2, "frame_rate":120, "low_latency_mode":true,
//    "default_frame_gen_on":false, "user_select_clarity":true, …}
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include "glt/core/db.hpp"

namespace glt::perf {

// ── Clarity tiers ────────────────────────────────────────────────────────────
// Render-resolution tier. Undocumented by Tencent; the mapping below is
// empirical and machine-independent (validated on this engine, not this GPU).
enum class Clarity {
    Low = 1,      // measurably LAGGIER than High — pushes work to the CPU
    Sweet = 2,    // current recommendation: clearer texture AND more FPS
    High = 3,
};

// ── Perf flag keys ───────────────────────────────────────────────────────────
// GameLoop names these exactly; do not "tidy" them.
inline constexpr std::string_view kLowLatency = "low_latency_mode";
inline constexpr std::string_view kFrameGen = "default_frame_gen_on";
inline constexpr std::string_view kClarity = "clarity";
inline constexpr std::string_view kFrameRate = "frame_rate";
inline constexpr std::string_view kUserSelectClarity = "user_select_clarity";

// Flag values the fixes force.
inline constexpr bool kLowLatencyValue = true;
inline constexpr bool kFrameGenValue = false;

// ── Plain-language explanations shown in the UI ──────────────────────────────

// Keyed by the flag name above. Returns "" for an unknown key rather than
// throwing, so a future GameLoop flag degrades to a bare row.
[[nodiscard]] std::string explain(std::string_view key);

[[nodiscard]] const std::vector<std::pair<std::string, std::string>>& explain_table();

// ── Reads ────────────────────────────────────────────────────────────────────

// Raw JSON text of androws.<pkg>.local_user_config, or "{}".
[[nodiscard]] std::string local_user_config_raw(const db::Connection& con, std::string_view pkg);

// Raw JSON text of androws.<pkg>.host_config, or "{}".
[[nodiscard]] std::string host_config_raw(const db::Connection& con, std::string_view pkg);

// Current clarity tier, or -1 when unset / unparseable.
[[nodiscard]] int current_clarity(const db::Connection& con, std::string_view pkg);

// ── Operations ───────────────────────────────────────────────────────────────

// Set `clarity`. Uses read-modify-write so every other field GameLoop keeps in
// that document (resolution arrays, toolbar state, fps fields) is preserved.
[[nodiscard]] db::Op set_clarity_op(std::string_view pkg, std::string_view current_json,
                                    int level);

// Set a boolean perf flag. Same read-modify-write contract.
[[nodiscard]] db::Op set_flag_op(std::string_view pkg, std::string_view current_json,
                                 std::string_view flag, bool value);

// Post-write proof that the value landed. This is the verify callback handed to
// guarded_updates(): it re-reads through a fresh query so a MATCH proves the
// bytes are in the file, not merely in our buffer.
[[nodiscard]] std::vector<std::string> clarity_verify(const db::Connection& con,
                                                      std::string_view pkg, int level);

} // namespace glt::perf