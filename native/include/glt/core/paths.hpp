// ─────────────────────────────────────────────────────────────────────────────
// glt/core/paths.hpp — portable path discovery.
//
// Ported from legacy-python/src/core/paths.py. The contract is unchanged and
// load-bearing: NO hardcoded usernames, drive letters or GameLoop versions. The
// Python build enumerated GetLogicalDrives() and looked for
//   <drive>\Program Files\Tencent\GameLoop
//   <drive>\Tencent\GameLoop
// across every fixed volume, plus the three ProgramFiles environment
// variables. That exact candidate list and that exact preference order are
// reproduced here, because changing it changes which install gets modified on
// a machine that has both a C: and a D: GameLoop.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <string_view>

namespace glt::paths {

inline constexpr std::string_view kApp = "GameLoop";

// The CODM Mobile package whose keymaps we tune.
//
// This is UTF-8 (a std::string_view), not a wstring_view, because every
// consumer of it builds a Configs.db KEY — and the whole key/value layer works
// in UTF-8. smk_conf() converts it once where it needs a wide path.
inline constexpr std::string_view kPkg = "com.activision.callofduty.shooter";

// %APPDATA%\Tencent\GameLoop\db\Configs.db, or "" when absent.
// NOTE: the Python build returned '' rather than throwing when the file was
// missing, and every caller branches on that to show "is GameLoop installed?".
[[nodiscard]] std::wstring appdata_db();

// %APPDATA%\Tencent\GameLoop\config\<pkg>\smk.conf, or "" when absent.
[[nodiscard]] std::wstring smk_conf();

// Directory holding Configs.db (used for backups + the journal file).
[[nodiscard]] std::wstring db_dir();

// Root of the GameLoop installation, or "" when not found.
[[nodiscard]] std::wstring install_root();

// Newest <root>\Application\<ver> directory, or "" when absent.
//
// "Newest" is a NUMERIC component-wise compare, not a lexical one. The Python
// original used sorted(glob(...))[-1], which put "10.0.0.0" before "7.0.167.0"
// and picked the wrong build after a major-version bump. See
// str::version_less().
[[nodiscard]] std::wstring app_version_dir(std::wstring_view root = {});

// <root>\Application\<ver>\<name> when that file exists, else "".
[[nodiscard]] std::wstring exe_in_build(std::wstring_view name, std::wstring_view root = {});

// Primary launcher executable for starting GameLoop (GameLoopLauncher.exe / GameLoop.exe / AppMarket.exe)
[[nodiscard]] std::wstring launcher_exe(std::wstring_view root = {});

// adb.exe inside the current build directory — the guest-side verification tool.
[[nodiscard]] std::wstring adb_exe(std::wstring_view root = {});

// <root>\Application\<ver>\ano_tmp — the engine's LOCAL template pack directory.
// This is the directory quarantine moves files out of; it only exists on
// builds that ship the packed keymap configs.
[[nodiscard]] std::wstring ano_tmp_dir();

// <root>\Application\<ver>\ano_tmp_quarantine — where quarantine moves them to.
[[nodiscard]] std::wstring ano_tmp_quarantine_dir();

// Registry-derived install location, as a fallback when no fixed-drive scan
// finds it. Reads HKLM\SOFTWARE\Tencent\GameLoop (and the WOW6432Node mirror).
[[nodiscard]] std::wstring install_root_from_registry();

struct HostInfo {
    std::wstring machine; // "AMD64" / "x86" / "ARM64"
    std::wstring release; // "10.0.19045"
    std::wstring version; // "10.0.19045.4291"
};

[[nodiscard]] HostInfo host_info();

} // namespace glt::paths