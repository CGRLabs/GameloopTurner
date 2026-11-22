// ─────────────────────────────────────────────────────────────────────────────
// glt/core/preflight.hpp — "does this install still look like the one the
// fixes were reverse-engineered against?"
//
// WHY THIS EXISTS
// ---------------
// The whole tool rests on a set of schema assumptions verified against live
// GameLoop builds (see docs/keymap-auto-switch.md): two tables, their column
// layout, the keymap key namespaces, and smk.conf carrying <Tex> templates.
// A GameLoop update that changes any of them would turn fixes into quiet
// no-ops — writes that succeed but mean nothing, or reads that find nothing.
// That failure mode is the worst one this tool has: the user believes the fix
// is applied.
//
// The preflight converts that into a loud, actionable verdict BEFORE any
// write: every check is read-only, the overall verdict gates Apply All, the
// Dashboard shows it as a card, and the diagnostic report carries it so a
// broken install can be supported from the report alone.
//
// The checks deliberately say NOTHING about specific mode/solution IDs — those
// are discovered at runtime by design (keymaps.hpp). This file only verifies
// the structural shape those discoveries rely on.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <vector>

#include "glt/core/db.hpp"

namespace glt::preflight {

enum class Status { Ok, Warn, Fail };

struct Check {
    std::string name;    // stable English label; also lands in the report
    Status status;
    std::string detail;  // "" when OK; human-readable reason when not
};

struct Report {
    Status overall = Status::Ok;
    std::vector<Check> checks;

    // One-line verdict for the UI: "all checks passed", "2 warning(s)",
    // "3 check(s) failed".
    [[nodiscard]] std::string summary_line() const;
};

// Runs every structural check against the live install. Read-only: it opens
// Configs.db the same way the Dashboard gather does (5 s busy timeout) and
// never writes. Returns after a few ms in the common case.
[[nodiscard]] Report run();

// ── Testable halves ─────────────────────────────────────────────────────────
//
// run() has two halves with different testability. The DB checks are pure
// functions of an open connection, so the unit tests build fixture databases
// and exercise the pass/warn/fail matrix directly. The smk.conf checks are
// pure functions of one file's bytes. Only the discovery + wiring glue in
// run() itself touches the real machine.
[[nodiscard]] Report run_db_checks(const db::Connection& con);
[[nodiscard]] std::vector<Check> run_smk_checks(const std::wstring& smk_path);

// ── GameLoop version checks ──────────────────────────────────────────────
//
// Reads HKLM\SOFTWARE\Tencent\GameLoop\Version and HKCU\...\GameLoop\Version.
// Warns when the running build has not been validated against the tuner's
// reverse-engineered assumptions (schema, Tex ids, mode ids).
[[nodiscard]] std::vector<Check> run_version_checks();

// Versions the tuner's keymap fixes have been validated against. Keeping this
// small means every entry is a build we actually tested; an unlisted build
// produces a Warn rather than a Fail, so the user can still apply but sees the
// caveat on the Dashboard Install Health card.
[[nodiscard]] const std::vector<std::string>& validated_gameloop_versions();

} // namespace glt::preflight
