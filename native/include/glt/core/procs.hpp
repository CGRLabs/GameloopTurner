// ─────────────────────────────────────────────────────────────────────────────
// glt/core/procs.hpp — GameLoop process + service control.
//
// Ported from legacy-python/src/core/procs.py, which shelled out to
// tasklist / taskkill / sc. Everything here is a direct Win32 call.
//
// THE CLOSE LADDER — WHY IT IS FIVE STAGES AND NOT THREE
// --------------------------------------------------------
// GameLoop is a multi-process app that can be in four different ownership
// states, and each stage exists because the one before it provably fails in
// some state:
//
//   1. WM_CLOSE to every top-level window   — reaches hidden/tray windows that
//      taskkill cannot target politely. Works without admin when the UI runs
//      as the interactive user. Fails when the UI is ELEVATED and we are not:
//      UIPI silently drops the message.
//   2. TerminateProcess (recursive tree)    — the /T equivalent. Must walk
//      th32csParentProcessID: a bare TerminateProcess on gameloop.exe leaves
//      the VM child alive, and the ladder then reports "still running"
//      forever. Requires SeDebugPrivilege to reach SYSTEM-owned processes.
//   3. TerminateProcess, same pass, already forced — reached only when stage
//      2 ran without /F. In the native version stage 2 IS the force, so this
//      stage is the same code path re-entered after the privilege retry.
//   4. WM_QUIT to every UI thread          — a true tray right-click needs UI
//      Automation (fragile across Win10/11 overflow menus). WM_QUIT reaches the
//      Qt message loop and is the closest reliable programmatic "Quit". This
//      is the stage that makes the tray case work at all.
//   5. OpenSCManager → ControlService(STOP) — optional, only with explicit
//      user consent, because GameLoop auto-starts and this changes machine
//      state beyond the current session.
//
// The Python ordering is preserved exactly; the comments above explain the
// "why" that the original encoded in its ladder.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace glt::procs {

// ── Identity ─────────────────────────────────────────────────────────────────

inline constexpr std::wstring_view kService = L"GameLoopService";
inline constexpr std::wstring_view kServiceExe = L"gameloopservice.exe";

// Image names we consider "GameLoop". procs.py filtered
// name.lower().startswith('gameloop') and excluded anything containing 'tuner',
// which is how the running tuner avoided terminating itself.
[[nodiscard]] bool is_gameloop_image(std::wstring_view image_name);

// ── Enumeration ──────────────────────────────────────────────────────────────

struct ProcInfo {
    std::wstring name;     // full image name, e.g. L"GameLoopVm.exe"
    std::uint32_t pid = 0;
    std::uint64_t mem_kb = 0;    // working set; 0 when it could not be read
    std::uint32_t parent_pid = 0; // for the recursive tree walk
};

// Every GameLoop* process except the tuner's own image.
[[nodiscard]] std::vector<ProcInfo> list_gameloop();

// As above, minus gameloopservice.exe. The service is a background daemon that
// should keep running unless the user explicitly asks to stop it.
[[nodiscard]] std::vector<ProcInfo> running(bool exclude_service = true);

struct ServiceStatus {
    // Numeric SCM state. The Python build returned the raw `sc query` line
    // ("STATE : 4  RUNNING") and the dashboard string-matched "running" in it;
    // this returns the DWORD and the dashboard compares numerically instead.
    std::uint32_t current_state = 0;
    std::uint32_t start_type = 0;
    bool found = false;

    [[nodiscard]] bool is_running() const;
    [[nodiscard]] std::wstring state_text() const;   // "RUNNING", "STOPPED", …
    [[nodiscard]] std::wstring start_type_text() const; // "AUTO_START", …
};

[[nodiscard]] ServiceStatus service_status();

// ── The ladder ───────────────────────────────────────────────────────────────

struct CloseResult {
    bool ok = false;
    std::vector<std::string> notes;
    std::vector<ProcInfo> left; // non-empty ⇒ the ladder did not fully close
};

struct CloseOptions {
    int grace_wait_s = 6;       // stage 1 window
    int kill_wait_ms = 3000;    // settle time after each terminate pass
    bool force = true;          // escalate straight to force
    bool stop_service_now = false; // stage 5
};

// Runs the ladder. Never throws — every failure becomes a note.
[[nodiscard]] CloseResult close_gameloop(const CloseOptions& opts = {});

// Stages, exposed for the "Close GameLoop" sidebar action and the tests.
[[nodiscard]] std::vector<ProcInfo> graceful_close(int wait_s);
[[nodiscard]] std::vector<ProcInfo> kill_pids(const std::vector<std::uint32_t>& pids, bool force,
                                              int wait_ms);
[[nodiscard]] std::vector<std::string> tray_quit_fallback(int wait_s = 2);
[[nodiscard]] std::string stop_service(int wait_s = 4);

// Enables SeDebugPrivilege on the current token.
//
// Required to terminate GameLoop's LocalSystem-spawned mid-match processes.
// Without it stage 2 fails with ERROR_ACCESS_DENIED on exactly the processes
// the user most needs closed. Returns false if the privilege is unavailable
// (e.g. a locked-down domain policy), which the ladder treats as "try anyway".
[[nodiscard]] bool enable_debug_privilege();

// ── Priorities ───────────────────────────────────────────────────────────────

inline constexpr std::wstring_view kPrioHigh = L"high";
inline constexpr std::wstring_view kPrioAbove = L"above";
inline constexpr std::wstring_view kPrioNormal = L"normal";
inline constexpr std::wstring_view kPrioBelow = L"below";

// image name (lowercase, with .exe) → priority name.
// Carried over from procs.py's default plan: the emulator and VM get High (they
// are the game), the assistant Above, the download service Below so a patch
// fetch cannot steal a core mid-match.
[[nodiscard]] std::vector<std::pair<std::wstring, std::wstring>> default_priority_plan();

[[nodiscard]] std::vector<std::string> set_priorities(
    const std::vector<std::pair<std::wstring, std::wstring>>& plan = {});

// ── Host ─────────────────────────────────────────────────────────────────────

struct HostResources {
    int cores = 0;
    int mem_gb = 0;
};

// Logical processor count via GetSystemInfo, total physical RAM in whole GB
// via GlobalMemoryStatusEx. mem_gb is floored, matching Python's // (1024**3).
[[nodiscard]] HostResources host_resources();

} // namespace glt::procs