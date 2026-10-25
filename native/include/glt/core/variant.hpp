// ─────────────────────────────────────────────────────────────────────────────
// glt/core/variant.hpp — which GameLoop build the user is tuning.
//
// STRATEGY (docs/targets/README.md)
// ---------------------------------
// v1 does NOT auto-detect the variant. There are two build-time variants and
// the user picks their GameLoop type once, on first launch. The choice is
// persisted so the question is never asked again unless the user clears it.
//
// Two independent notions, deliberately kept apart:
//
//   * compiled()    — which implementation is baked into THIS binary. Set by
//                     the CMake option (GLT_TARGET_BETA / GLT_TARGET_HUB) as a
//                     compile-time macro. This is what governs behaviour:
//                     paths, process lists, SQL, perf flags. It never changes at
//                     runtime, because v1 has no runtime branching for those.
//   * the user's    — which GameLoop the user actually has, recorded by the
//     persisted       first-launch selector. It does not switch code paths; it
//     choice          is the record that lets the UI warn when the user is
//                     running the "wrong" build for their install.
//
// The persisted default is always Beta, matching docs/targets/beta/PLAN.md.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace glt::variant {

enum class Id {
    Beta, // "GameLoop Beta"        — the original, validated target
    Hub,  // "GameLoop Hub (TAC)"   — the Arena / multi-app build
};

// User-facing label, exactly as the selector and About page show it.
[[nodiscard]] const char* label(Id id);

// Stable, filesystem-safe identifier written to the choice file. Changing one
// orphans a user's persisted choice, so these are load-bearing.
[[nodiscard]] const char* key(Id id);

// The variant this binary was compiled for. Selected by CMake at build time.
[[nodiscard]] Id compiled();

// The variant assumed when nothing has been persisted: Beta.
[[nodiscard]] Id default_id();

// "beta" / "hub" (case-insensitive, trimmed) -> Id. Anything else -> nullopt,
// which is what makes a corrupt or hand-edited choice file fall back to the
// default instead of failing to start.
[[nodiscard]] std::optional<Id> parse(std::string_view text);

// ── Persistence ──────────────────────────────────────────────────────────────

// %APPDATA%\GameLoopTuner\variant.txt — deliberately NOT inside GameLoop's own
// %APPDATA%\Tencent\GameLoop tree, which the engine owns and may recreate.
[[nodiscard]] std::wstring choice_file();

// The persisted choice, or nullopt when the user has never chosen.
[[nodiscard]] std::optional<Id> load_choice();

// Writes the choice (creating the directory if needed). Returns false when the
// file could not be written, in which case the caller keeps the in-memory
// choice for the session but will ask again next launch.
[[nodiscard]] bool save_choice(Id id);

// Removes the choice file. The next launch re-asks.
[[nodiscard]] bool clear_choice();

// Path-explicit forms, so the round-trip is testable without touching the
// real %APPDATA% of whoever runs the tests.
[[nodiscard]] std::optional<Id> load_choice_from(const std::wstring& path);
[[nodiscard]] bool save_choice_to(const std::wstring& path, Id id);

} // namespace glt::variant