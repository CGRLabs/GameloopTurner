// ─────────────────────────────────────────────────────────────────────────────
// glt/core/log.hpp — rotating file log + in-memory ring for the UI.
//
// Ported from legacy-python/src/core/log.py.
//
// Two sinks, same format:
//   * %TEMP%\gameloop-tuner\tuner.log, rotating at 1 MB x 3
//   * a 500-entry in-memory ring the Logs page and the diagnostic report read
//
// Everything written to either sink passes through str::scrub_secrets() first.
// The report gets attached to a public Discord thread, so a leaked device ID
// is a real exposure, not a theoretical one.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace glt::log {

enum class Level { Info, Success, Warning, Error, Progress, Drift };

[[nodiscard]] std::string_view level_tag(Level l);

// Thread-safe. Opens the rotating file on first use; every failure path is
// silent because logging must never be the reason the app dies.
void write(Level level, std::string_view message);

void info(std::string_view msg);
void warn(std::string_view msg);
void error(std::string_view msg);

// The ring, oldest first. Capped at 500 entries (ported from log.py's
// `del _mem[:-500]`).
[[nodiscard]] std::vector<std::string> mem_lines();

// Clear the ring. The rotating file is untouched.
void clear_mem();

// Absolute path of the rotating file, whether or not it exists yet.
[[nodiscard]] std::wstring file_path();

} // namespace glt::log