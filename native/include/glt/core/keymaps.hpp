// ─────────────────────────────────────────────────────────────────────────────
// glt/core/keymaps.hpp — the reverse-engineered GameLoop keymap logic.
//
// Ported 1:1 from legacy-python/src/core/keymaps.py. This module is the part of
// the app where a "small" divergence silently disables the fix, so the SQL, the
// key naming and the rule counting are reproduced exactly. See
// docs/keymap-auto-switch.md for the reverse-engineering trail.
//
// SCHEMA REMINDER (verified against a live Configs.db)
// -----------------------------------------------------------
//   key_map_configs_cache_official (key, value0, value1)
//     keymap_mode.unlogin.<pkg>.<mode>         JSON  {lastSolutionId, solutions{}}
//     keymap_data.unlogin.<pkg>.<mode>.<sol>   XML   user blob  — WRITABLE
//     offcial_keymap_data2.<pkg>.<mode>.<sol>  XML   official   — trigger-guarded
//     offcial_config2.<pkg>                    value1 = <Switch ModeID="N"> table
//     lastmodeid.unlogin.<pkg>                 "145"
//     recentlyused_mode.<pkg>                  JSON  {"145": <timestamp>}
//   configs (key, value)
//     androws.<pkg>.smk                        XML   <Tex id=...> templates
//     androws.<pkg>.local_user_config          JSON  clarity, low_latency_mode…
//     androws.<pkg>.host_config                JSON  default_frame_rate…
//
// 'offcial' is GameLoop's own misspelling and must not be corrected.
//
// KNOWN MODE / SOLUTION IDs (discovered generically at runtime, these are the
// values observed on this machine — the code never hardcodes them):
//   145      Multiplayer      flat sol 1792013098464  "mpkeymap"
//     └ 1768790151264  "Multi Player"  93 rules — must ALSO be flattened
//     └ 1792013096641  "brkeymap"
//   146      Battle Royale   flat sol 146
//   531686   DMZ             flat sol 1063553
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "glt/core/db.hpp"

namespace glt::keymaps {

inline constexpr std::string_view kCacheTable = "key_map_configs_cache_official";

// ── Structures ───────────────────────────────────────────────────────────────

// One row from keymap_data.unlogin.<pkg>.<mode>.<sol>.
struct Solution {
    std::string mode;
    std::string sol;   // solution id, always a string (they exceed int64 in places)
    std::string name;  // solutionName from the mode JSON
    int rule_count = 0; // count of "SwitchOperation" in the blob; 0 == "flat"
};

// The parsed keymap_mode.unlogin.<pkg>.<mode> JSON.
struct ModeInfo {
    std::string mode;
    std::optional<std::string> last_solution_id;
    std::optional<int> last_solution_id_type;
    int mode_id = 0;
    // solutionId → {name, sourceType}
    std::vector<std::pair<std::string, std::pair<std::string, int>>> solutions;
};

// ── Key-naming ───────────────────────────────────────────────────────────────
//
// Every read and write goes through these. The key strings are scattered across
// two tables with two different prefixes ('keymap_*' vs 'androws.*') and one
// notoriously misspelled one ('offcial'), so building them anywhere else is how
// a typo becomes a silent no-op that reads as "the fix did nothing".
//
//   key_map_configs_cache_official
//     keymap_mode.unlogin.<pkg>.<mode>          JSON
//     keymap_data.unlogin.<pkg>.<mode>.<sol>    user blob XML   (WRITABLE)
//     offcial_keymap_data2.<pkg>.<mode>.<sol>   official XML    (trigger-guarded)
//     offcial_config2.<pkg>                     jump table      (value1)
//     lastmodeid.unlogin.<pkg>
//     recentlyused_mode.<pkg>
//   configs
//     androws.<pkg>.smk
//     androws.<pkg>.local_user_config
//     androws.<pkg>.host_config

[[nodiscard]] std::string mode_key(std::string_view pkg, std::string_view mode);
[[nodiscard]] std::string blob_key(std::string_view pkg, std::string_view mode,
                                   std::string_view sol);
[[nodiscard]] std::string official_blob_key(std::string_view pkg, std::string_view mode,
                                            std::string_view sol);
[[nodiscard]] std::string official_config_key(std::string_view pkg);
[[nodiscard]] std::string lastmodeid_key(std::string_view pkg);
[[nodiscard]] std::string recentlyused_key(std::string_view pkg);
[[nodiscard]] std::string smk_key(std::string_view pkg);
[[nodiscard]] std::string local_user_config_key(std::string_view pkg);

// ── Typed reads ──────────────────────────────────────────────────────────────

// nullopt means "no such row", which is distinct from "present but empty".
[[nodiscard]] std::optional<std::string> read_official_blob(const db::Connection& con,
                                                             std::string_view pkg,
                                                             std::string_view mode,
                                                             std::string_view sol);
[[nodiscard]] std::optional<std::string> read_user_blob(const db::Connection& con,
                                                        std::string_view pkg,
                                                        std::string_view mode,
                                                        std::string_view sol);
[[nodiscard]] std::optional<std::string> read_lastmodeid(const db::Connection& con,
                                                         std::string_view pkg);
[[nodiscard]] std::optional<std::string> read_recentlyused(const db::Connection& con,
                                                           std::string_view pkg);
[[nodiscard]] std::optional<std::string> read_smk(const db::Connection& con,
                                                  std::string_view pkg);

// ── Discovery ────────────────────────────────────────────────────────────────

// keymap_mode.unlogin.<pkg>.<mode> JSON for every mode, keyed by mode id.
// Unparseable rows are skipped rather than failing the whole query — one bad
// mode must not hide the twelve good ones.
[[nodiscard]] std::vector<ModeInfo> modes(const db::Connection& con, std::string_view pkg);

// Every (mode, solution) that has a user blob, with its SwitchOperation count.
//
// The "flat" test is blob.count("SwitchOperation") == 0. That is the exact
// predicate the Python build used, and it is the definition of a layout that
// cannot auto-switch: with zero switch rules there is nothing for the engine
// to remap onto a different layout.
[[nodiscard]] std::vector<Solution> solutions(const db::Connection& con, std::string_view pkg);

// Picks the 0-rule layout to pin. Preference order 'mp' → 'multi' → 'br',
// case-insensitive substring on the solution NAME, falling back to the first
// flat layout found. Generic by design — no solution IDs are hardcoded.
[[nodiscard]] std::optional<Solution> pick_flat(const std::vector<Solution>& rows,
                                                std::string_view prefer = "mp");

// ── Jump-table introspection ─────────────────────────────────────────────────

// ModeIDs referenced by <Switch … ModeID="N"> in offcial_config2.<pkg>.
// These are the scene→mode jumps the Tex templates feed.
[[nodiscard]] std::vector<std::string> jump_tags(const db::Connection& con, std::string_view pkg);

// The raw <Switch …> elements, for the diagnostic report.
[[nodiscard]] std::vector<std::string> jump_tags_detail(const db::Connection& con,
                                                        std::string_view pkg);

// Tex id → TextureId. Proven mapping: Tex id = 10000 + TextureId.
[[nodiscard]] constexpr int tex_to_switch(int tex_id) { return tex_id - 10000; }

// ── smk.conf text surgery ────────────────────────────────────────────────────

// A <Tex id="N" … comm="…"/> element located inside a larger document.
struct TexMatch {
    std::size_t begin = 0;  // offset of '<'
    std::size_t end = 0;    // one past '/>'
    std::string comm;       // the comm attribute, used as the log label
};

// Finds and removes exactly one <Tex id="N"> element, returning the new text
// and its description.
//
// The element really is laid out across lines in smk.conf:
//     <Tex id="10001" width="1024" … path="225154410"
//         comm="开关门"/>
// so `comm` sits on the FOLLOWING line behind a tab. This is hand-written
// rather than std::regex because std::regex is both slower and fussier about
// the \n?\s* that this exact layout needs.
//
// Returns false and leaves `text` untouched when the element is absent, which
// is the idempotent path the UI reports as "already absent".
[[nodiscard]] bool blind_tex_in_text(std::string& text, int tex_id, TexMatch* out = nullptr);

// Every Tex id present in a document, ascending, unique.
[[nodiscard]] std::vector<int> tex_ids_in_text(std::string_view text);

// ── Blob surgery ─────────────────────────────────────────────────────────────

// Strips every <SwitchOperation> from an official blob and wraps the remainder
// in a fresh <KeyMapSolution> envelope. Handles both the self-closing form
// (<SwitchOperation …/>) and the paired form with a body.
[[nodiscard]] std::string flatten_blob(std::string_view official_xml, std::string_view sol_id,
                                       std::string_view sol_name);

// Same stripping, but applied IN PLACE to an existing USER blob, preserving
// its own <KeyMapSolution> wrapper. This is the path that matters for mode 145,
// where re-wrapping would discard fields GameLoop wrote itself.
[[nodiscard]] std::string flatten_in_place(std::string_view user_blob);

// ── Operations (SQL + params, no execution) ──────────────────────────────────

// Points a mode's active solution at `sol` and sets lastmodeid to `mode`.
[[nodiscard]] std::vector<db::Op> pin_flat_ops(db::Connection& con, std::string_view pkg,
                                               std::string_view mode, std::string_view sol);

// Creates a synthetic mode entry for a mode the user does not have configured
// (the DMZ/BR destinations), so the engine accepts the blob we are about to
// write. Type 1 = "created by the app", as opposed to type 0 = "engine".
[[nodiscard]] std::vector<db::Op> register_mode_ops(std::string_view pkg, std::string_view mode,
                                                    std::string_view sol, std::string_view name);

// Strips SwitchOperations from an existing user blob, keeping its envelope.
// Returns {} when the blob is missing or already flat.
[[nodiscard]] std::vector<db::Op> flatten_user_blob_ops(db::Connection& con, std::string_view pkg,
                                                         std::string_view mode,
                                                         std::string_view sol);

// Writes recentlyused_mode.<pkg> = {"145": 0}, clearing the engine's jump
// history. Purely cosmetic — the engine refills it in memory during play and
// drift detection deliberately does not track it.
[[nodiscard]] db::Op scrub_recent_op(std::string_view pkg, int keep_mode = 145);

// Writes androws.<pkg>.smk with the given (already-blinded) document.
[[nodiscard]] db::Op set_smk_op(std::string_view pkg, std::string_view smk_xml);

// Writes (or replaces) the USER blob for a mode/solution. This is the only
// keymap write the tuner performs — the official rows are trigger-guarded and
// GameLoop raises "Resmi yapilandirma degerleri degistirilemez." on them.
[[nodiscard]] db::Op set_user_blob_op(std::string_view pkg, std::string_view mode,
                                       std::string_view sol, std::string_view blob);

// ── Drift report ─────────────────────────────────────────────────────────────

struct DriftReport {
    std::optional<std::string> lastmodeid;
    std::optional<std::string> recentlyused; // raw JSON text
    std::optional<std::string> mode145_solution;
    std::optional<std::string> mode145_name;
};

[[nodiscard]] DriftReport drift_report(const db::Connection& con, std::string_view pkg);

} // namespace glt::keymaps