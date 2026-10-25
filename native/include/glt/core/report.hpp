// ─────────────────────────────────────────────────────────────────────────────
// glt/core/report.hpp — one-file diagnostic dump.
//
// Ported from legacy-python/src/core/report.py. The output is a plain-text
// file the user attaches to a bug report, so it is built for a stranger to
// read: every section labelled, every value scrubbed, hard-capped at 200 KB.
//
// SECURITY: the whole document passes through str::scrub_secrets() before it
// is returned, and report.py's SKIP_REG_KEYS are dropped outright rather than
// masked. Certificate hashes, device GUIDs and IP coordinates have no business
// in a public Discord thread.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>

namespace glt::report {

inline constexpr const char* kVersion = "2.0.0";

// Registry values that are dropped rather than redacted.
[[nodiscard]] bool is_sensitive_key(const std::string& key);

// The full report text (already scrubbed).
[[nodiscard]] std::string build();

// Writes to <Desktop>\GameLoopTuner-report-YYYYMMDD-HHMMSS.txt, capped at
// 200 KB. Returns "" on failure.
[[nodiscard]] std::string export_to_desktop();

} // namespace glt::report