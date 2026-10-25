// ─────────────────────────────────────────────────────────────────────────────
// native/installer/setup.hpp — shared declarations for the GameLoop Tuner setup.
//
// This is deliberately NOT linked against glt_core. The whole point of a custom
// installer is that it stays small: it has to pull in nothing but Win32, GDI,
// Shell and Advapi32, so a full re-link costs ~100 KB instead of ~1.7 MB.
//
// The palette below is a transcription of glt/ui/theme.hpp rather than a shared
// header, because sharing it would drag <imgui.h> (and the DX11 backend) into
// this binary. The values are the same; if you restyle the app, restyle here.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

// Also set on the command line; guarded so the two never collide.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

// Set by CMake from project(... VERSION ...). Kept narrow and widened on demand
// so there is exactly one place the version is declared.
#ifndef GLT_VERSION
#define GLT_VERSION "0.0.0"
#endif

namespace setup {

// ── Identity ──────────────────────────────────────────────────────────────────
//
// These strings are load-bearing: the uninstall registry key is derived from
// kUninstallKeyName, so renaming it orphans every existing installation.
inline constexpr wchar_t kProductName[]    = L"GameLoop Tuner";
inline constexpr wchar_t kPublisher[]      = L"realLLUKS";
// The installed app file keeps the original name regardless of which variant
// payload was shipped, so shortcuts, the running-check and the uninstall entry
// stay stable across a Beta -> Hub reinstall. run_install() accepts any of the
// variant payload names below and installs it under kExeName.
inline constexpr wchar_t kExeName[]        = L"GameLoopTuner.exe";
inline constexpr wchar_t kExeNameBeta[]    = L"GameLoopTuner-Beta.exe";
inline constexpr wchar_t kExeNameHub[]     = L"GameLoopTuner-Hub.exe";
inline constexpr wchar_t kSetupName[]      = L"GameLoopTunerSetup.exe";
inline constexpr wchar_t kInstallDirName[] = L"GameLoop Tuner";
inline constexpr wchar_t kUninstallKeyName[] = L"GameLoopTuner";

inline const char* version() { return GLT_VERSION; }

// ── Palette (mirrors glt/ui/theme.hpp) ────────────────────────────────────────
inline constexpr COLORREF kBgPrimary   = RGB(0x1C, 0x1C, 0x1F);
inline constexpr COLORREF kBgSidebar   = RGB(0x18, 0x18, 0x1B);
inline constexpr COLORREF kBgCard      = RGB(0x24, 0x24, 0x29);
inline constexpr COLORREF kBgHover     = RGB(0x2B, 0x2B, 0x32);
inline constexpr COLORREF kBgActive    = RGB(0x34, 0x34, 0x3F);
inline constexpr COLORREF kBorder      = RGB(0x32, 0x32, 0x3A);
inline constexpr COLORREF kAccent      = RGB(0xF5, 0x9E, 0x0B);
inline constexpr COLORREF kAccentHover = RGB(0xFB, 0xBF, 0x24);
inline constexpr COLORREF kOnAccent    = RGB(0x0B, 0x0C, 0x10);
inline constexpr COLORREF kOk          = RGB(0x10, 0xB9, 0x81);
inline constexpr COLORREF kError       = RGB(0xEF, 0x44, 0x44);
inline constexpr COLORREF kTextPrimary = RGB(0xF4, 0xF4, 0xF5);
inline constexpr COLORREF kTextMuted   = RGB(0xA1, 0xA1, 0xAA);
inline constexpr COLORREF kTextDim     = RGB(0x71, 0x71, 0x7A);

// ── Modes ─────────────────────────────────────────────────────────────────────

enum class Mode { Install, Uninstall };

struct Options {
    Mode mode       = Mode::Install;
    bool desktop    = true;   // create the desktop shortcut
    bool silent     = false;  // no UI; exit code carries the result
};

struct Result {
    bool ok = false;
    std::wstring message;     // user-facing, already localised to plain English
};

// ── Paths ─────────────────────────────────────────────────────────────────────

// Full path of this executable.
std::wstring self_path();

// The directory holding this executable, with a trailing backslash.
std::wstring self_dir();

// %ProgramFiles%\GameLoop Tuner
std::wstring install_dir();

// <install_dir>\GameLoopTuner.exe  (the app)
std::wstring installed_app();

// <install_dir>\GameLoopTunerSetup.exe  (this binary, copied in at install so
// the UninstallString in the registry keeps working after the setup that did
// the install has been moved or deleted)
std::wstring installed_setup();

// ── Helpers ───────────────────────────────────────────────────────────────────

std::wstring widen(const char* utf8);

bool ensure_dir(const std::wstring& path);

bool file_exists(const std::wstring& path);

// True when the installed copy of GameLoopTuner.exe is running. The app is
// elevated, so an unelevated probe cannot open it with PROCESS_QUERY_INFORMATION;
// PROCESS_QUERY_LIMITED_INFORMATION works across the integrity boundary.
bool app_running();

// ── Operations ────────────────────────────────────────────────────────────────

Result run_install(const Options& opt);
Result run_uninstall(const Options& opt);

} // namespace setup