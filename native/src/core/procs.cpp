#include "glt/core/procs.hpp"

#include "glt/core/log.hpp"
#include "glt/core/strings.hpp"

#include <windows.h>
// winsvc.h pulls in SERVICE_* / SC_MANAGER_* / QueryServiceConfig. It needs
// _WIN32_WINNT >= 0x0601 for SERVICE_AUTO_START_DELAYED; CMake sets that in
// CMakeLists.txt via WINVER / _WIN32_WINNT.
#include <winsvc.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <map>

namespace glt::procs {
namespace {

// Force-kill order: game first, UI last, so the emulator has the best chance of
// a clean flush before its window disappears. The service is NEVER in this
// list — stopping it is a separate, explicitly consented stage.
//
// Ported verbatim from procs.py::KILL_ORDER.
constexpr std::wstring_view kKillOrder[] = {
    L"gameloopvm.exe",          L"gameloopemulator.exe",
    L"gameloopassistant.exe",   L"gameloopassistanttoast.exe",
    L"gameloopdldsvr.exe",      L"gameloopvfs.exe",
    L"gameloopupdate.exe",      L"gameloopinstaller.exe",
    L"cefrendererprocess.exe",  L"gameloop.exe",
    L"gamelooplauncher.exe",
};

int order_key(std::wstring_view name) {
    const auto lower = str::ascii_lower(name);
    for (std::size_t i = 0; i < std::size(kKillOrder); ++i) {
        if (lower == kKillOrder[i]) return static_cast<int>(i);
    }
    return static_cast<int>(std::size(kKillOrder)); // unknown → last
}

DWORD priority_class(std::wstring_view name) {
    // procs.py::PRIO, verbatim.
    if (name == kPrioHigh)   return HIGH_PRIORITY_CLASS;    // 0x80
    if (name == kPrioAbove)  return ABOVE_NORMAL_PRIORITY_CLASS; // 0x8000
    if (name == kPrioBelow)  return BELOW_NORMAL_PRIORITY_CLASS; // 0x4000
    return NORMAL_PRIORITY_CLASS;                             // 0x20
}

// Our own image name, so list_gameloop() never reports the tuner.
std::wstring self_image() {
    static const std::wstring name = [] {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return str::ascii_lower(std::wstring_view(buf, n));
    }();
    return name;
}

// Walks every process in the system once and returns name/pid/parent triples.
std::vector<ProcInfo> snapshot_all() {
    std::vector<ProcInfo> out;

    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (::Process32FirstW(snap, &pe)) {
        do {
            ProcInfo p;
            p.name = pe.szExeFile;
            p.pid = pe.th32ProcessID;
            p.parent_pid = pe.th32ParentProcessID;
            out.push_back(std::move(p));
        } while (::Process32NextW(snap, &pe));
    }

    ::CloseHandle(snap);
    return out;
}

// Working set in KB. Toolhelp32 does not report it, so psapi is used — which is
// why psapi.lib is on the link line.
std::uint64_t working_set_kb(std::uint32_t pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;

    PROCESS_MEMORY_COUNTERS pmc{};
    const BOOL ok = ::GetProcessMemoryInfo(h, &pmc, sizeof(pmc));
    ::CloseHandle(h);
    if (!ok) return 0;
    return pmc.WorkingSetSize / 1024;
}

// CALLBACK for EnumWindows / EnumThreadWindows. A plain function pointer with
// no capture; the PID set lives in thread_local storage for the duration.
//
// PIDs are stored as DWORD rather than uint32_t: GetWindowThreadProcessId
// hands back a DWORD, and DWORD is `unsigned long` on MSVC while uint32_t is
// `unsigned int` — different types, so a std::vector of one will not bind to a
// pointer to the other.
struct EnumCtx {
    const std::vector<DWORD>* pids = nullptr;
    std::vector<HWND> windows;
    std::vector<DWORD> threads;
};

thread_local EnumCtx* g_enum_ctx = nullptr;

BOOL CALLBACK enum_window_cb(HWND hwnd, LPARAM) {
    if (!g_enum_ctx || !g_enum_ctx->pids) return TRUE;

    DWORD pid = 0;
    const DWORD tid = ::GetWindowThreadProcessId(hwnd, &pid);
    const auto& want = *g_enum_ctx->pids;
    if (std::find(want.begin(), want.end(), pid) == want.end()) return TRUE;

    g_enum_ctx->windows.push_back(hwnd);
    if (tid) g_enum_ctx->threads.push_back(tid);
    return TRUE;
}

struct EnumGuard {
    EnumCtx* saved = nullptr;
    explicit EnumGuard(EnumCtx* c) : saved(g_enum_ctx) { g_enum_ctx = c; }
    ~EnumGuard() { g_enum_ctx = saved; }
};

} // namespace

// ── Identity ─────────────────────────────────────────────────────────────────

bool is_gameloop_image(std::wstring_view image_name) {
    const auto lower = str::ascii_lower(image_name);

    // Never report or kill the tuner itself. This was the same guard the
    // Python build had ('tuner' not in name.lower()).
    if (lower.find(L"tuner") != std::wstring::npos) return false;
    if (lower.find(L"glt") != std::wstring::npos) return false;
    if (lower == self_image()) return false;

    if (lower.rfind(L"gameloop", 0) == 0) return true;

    // Additional known GameLoop and Tencent emulator components
    constexpr std::wstring_view kKnown[] = {
        L"cefrendererprocess.exe",
        L"appmarket.exe",
        L"androidprocess.exe",
        L"aow_exe.exe",
        L"qmemsimulatorservice.exe",
        L"sysrestart.exe",
        L"txgameassistant.exe",
        L"tsettingcenter.exe",
    };
    for (const auto& k : kKnown) {
        if (lower == k) return true;
    }

    return false;
}

// ── Enumeration ──────────────────────────────────────────────────────────────

std::vector<ProcInfo> list_gameloop() {
    std::vector<ProcInfo> out;

    for (auto& p : snapshot_all()) {
        if (!is_gameloop_image(p.name)) continue;
        p.mem_kb = working_set_kb(p.pid);
        out.push_back(std::move(p));
    }
    return out;
}

std::vector<ProcInfo> running(bool exclude_service) {
    auto rows = list_gameloop();
    if (exclude_service) {
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [](const ProcInfo& p) {
                                      return str::ascii_lower(p.name) == kServiceExe;
                                  }),
                   rows.end());
    }
    return rows;
}

// ── Service ──────────────────────────────────────────────────────────────────

ServiceStatus service_status() {
    ServiceStatus out;

    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return out;

    SC_HANDLE svc = ::OpenServiceW(scm, std::wstring(kService).c_str(), SERVICE_QUERY_STATUS |
                                                                         SERVICE_QUERY_CONFIG);
    if (svc) {
        SERVICE_STATUS_PROCESS ssp{};
        DWORD needed = 0;
        if (::QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                                   reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) {
            out.current_state = ssp.dwCurrentState;
            out.found = true;
        }

        DWORD bytes = 0;
        if (::QueryServiceConfigW(svc, nullptr, 0, &bytes) == ERROR_INSUFFICIENT_BUFFER &&
            bytes > 0) {
            std::vector<BYTE> buf(bytes);
            auto* qsc = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buf.data());
            if (::QueryServiceConfigW(svc, qsc, bytes, &bytes)) {
                out.start_type = qsc->dwStartType;
            }
        }
        ::CloseServiceHandle(svc);
    }

    ::CloseServiceHandle(scm);
    return out;
}

bool ServiceStatus::is_running() const {
    return current_state == SERVICE_RUNNING || current_state == SERVICE_START_PENDING;
}

std::wstring ServiceStatus::state_text() const {
    if (!found) return L"NOT FOUND";
    switch (current_state) {
        case SERVICE_STOPPED:          return L"STOPPED";
        case SERVICE_START_PENDING:    return L"START_PENDING";
        case SERVICE_STOP_PENDING:     return L"STOP_PENDING";
        case SERVICE_RUNNING:          return L"RUNNING";
        case SERVICE_CONTINUE_PENDING: return L"CONTINUE_PENDING";
        case SERVICE_PAUSE_PENDING:    return L"PAUSE_PENDING";
        case SERVICE_PAUSED:           return L"PAUSED";
        default:                       return L"STATE_" + std::to_wstring(current_state);
    }
}

std::wstring ServiceStatus::start_type_text() const {
    switch (start_type) {
        case SERVICE_AUTO_START:       return L"AUTO_START";
        case SERVICE_DEMAND_START:     return L"MANUAL";
        case SERVICE_DISABLED:         return L"DISABLED";
        case SERVICE_BOOT_START:       return L"BOOT_START";
        case SERVICE_SYSTEM_START:     return L"SYSTEM_START";
        default:
            // SERVICE_AUTO_START_DELAYED needs _WIN32_WINNT >= 0x0601. It is
            // kept out of the switch above so the value still renders usefully
            // when the SDK in use predates it.
            if (start_type == 0x0002) return L"AUTO_START (delayed)";
            return L"START_TYPE_" + std::to_wstring(start_type);
    }
}

// ── Privilege ────────────────────────────────────────────────────────────────

bool enable_debug_privilege() {
    // NOTE: these are named kToken* rather than TOKEN_* on purpose. winnt.h
    // already defines TOKEN_QUERY and TOKEN_ADJUST as MACROS, so a
    // `constexpr DWORD TOKEN_QUERY = ...` here does not declare a variable — it
    // expands to `constexpr DWORD 0x0008 = ...` and the build stops with
    // "left operand must be l-value", pointing at the wrong line entirely.
    constexpr DWORD kTokenAdjust = 0x0020;
    constexpr DWORD kTokenQuery = 0x0008;
    constexpr DWORD kSeEnable = 0x0002;

    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), kTokenAdjust | kTokenQuery, &token)) {
        return false;
    }

    LUID luid{};
    if (!::LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        ::CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = kSeEnable;

    // AdjustTokenPrivileges reports success even when it silently assigns
    // ERROR_NOT_ALL_ASSIGNED, so the last-error is checked explicitly.
    SetLastError(ERROR_SUCCESS);
    const BOOL ok = ::AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
    const DWORD err = ::GetLastError();
    ::CloseHandle(token);

    return ok && err == ERROR_SUCCESS;
}

// ── Ladder stages ────────────────────────────────────────────────────────────

std::vector<ProcInfo> graceful_close(int wait_s) {
    std::vector<ProcInfo> live = running();
    if (live.empty()) return {};

    std::vector<DWORD> pids;
    pids.reserve(live.size());
    for (const auto& p : live) pids.push_back(static_cast<DWORD>(p.pid));

    // EnumWindows enumerates hidden and tray top-level windows too, which is
    // how this reaches the minimized-to-tray UI that taskkill cannot address
    // politely.
    EnumCtx ctx;
    ctx.pids = &pids;
    {
        EnumGuard guard(&ctx);
        ::EnumWindows(&enum_window_cb, 0);
    }

    for (HWND h : ctx.windows) {
        // If the GameLoop UI is running ELEVATED (mid-match, spawned by the
        // LocalSystem service), UIPI silently drops this message. That is why
        // the ladder escalates rather than treating stage 1 as authoritative.
        ::PostMessageW(h, WM_CLOSE, 0, 0);
    }

    if (wait_s > 0) ::Sleep(static_cast<DWORD>(wait_s) * 1000);
    return running();
}

std::vector<ProcInfo> kill_pids(const std::vector<std::uint32_t>& pids, bool force,
                                int wait_ms) {
    if (pids.empty()) return running();

    // Snapshot so we can order by KILL_ORDER and expand the tree.
    const auto all = snapshot_all();

    // pid → (name, parent). Used for the recursive descent.
    std::map<std::uint32_t, const ProcInfo*> by_pid;
    for (const auto& p : all) by_pid.emplace(p.pid, &p);

    // Expand each requested pid to its full descendant set.
    // THIS is what `taskkill /T` did. A bare TerminateProcess on gameloop.exe
    // would leave the VM child alive and the ladder would report "still
    // running" forever.
    std::vector<std::uint32_t> targets;
    auto descend = [&](std::uint32_t root, auto&& self) -> void {
        targets.push_back(root);
        for (const auto& p : all) {
            if (p.parent_pid == root) self(p.pid, self);
        }
    };
    for (std::uint32_t pid : pids) descend(pid, descend);

    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());

    // Group order: game first, UI last.
    std::stable_sort(targets.begin(), targets.end(), [&](std::uint32_t a, std::uint32_t b) {
        const auto ita = by_pid.find(a);
        const auto itb = by_pid.find(b);
        const int ka = ita == by_pid.end() ? 1 << 20 : order_key(ita->second->name);
        const int kb = itb == by_pid.end() ? 1 << 20 : order_key(itb->second->name);
        return ka < kb;
    });

    std::vector<HANDLE> open_handles;
    open_handles.reserve(targets.size());

    for (std::uint32_t pid : targets) {
        if (!force) {
            std::vector<DWORD> pidset{pid};
            EnumCtx ctx;
            ctx.pids = &pidset;
            {
                EnumGuard guard(&ctx);
                ::EnumWindows(&enum_window_cb, 0);
            }
            for (HWND h : ctx.windows) ::PostMessageW(h, WM_CLOSE, 0, 0);
        }

        HANDLE h = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                 pid);
        if (!h) {
            h = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        }
        if (!h) continue; // already gone, or we lack rights

        ::TerminateProcess(h, 1);
        open_handles.push_back(h);
    }

    // Wait for the terminated processes to actually unmap, release file locks,
    // and exit kernel structures so Configs.db / smk.conf are clean.
    for (HANDLE h : open_handles) {
        ::WaitForSingleObject(h, 1500);
        ::CloseHandle(h);
    }

    if (wait_ms > 0) ::Sleep(static_cast<DWORD>(wait_ms));
    return running();
}

std::vector<std::string> tray_quit_fallback(int wait_s) {
    std::vector<std::string> notes;

    const auto live = running();
    std::vector<DWORD> pids;
    pids.reserve(live.size());
    for (const auto& p : live) pids.push_back(static_cast<DWORD>(p.pid));
    if (pids.empty()) {
        if (wait_s > 0) ::Sleep(static_cast<DWORD>(wait_s) * 1000);
        return notes;
    }

    EnumCtx ctx;
    ctx.pids = &pids;
    {
        EnumGuard guard(&ctx);
        ::EnumWindows(&enum_window_cb, 0);
    }

    std::sort(ctx.threads.begin(), ctx.threads.end());
    ctx.threads.erase(std::unique(ctx.threads.begin(), ctx.threads.end()), ctx.threads.end());

    for (DWORD tid : ctx.threads) {
        // WM_QUIT reaches the Qt message loop and unwinds it cleanly — the
        // closest reliable programmatic equivalent of a tray right-click Quit.
        ::PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }

    notes.push_back("tray-quit fallback: WM_QUIT -> " + std::to_string(ctx.threads.size()) +
                    " UI thread(s)");

    if (wait_s > 0) ::Sleep(static_cast<DWORD>(wait_s) * 1000);
    return notes;
}

std::string stop_service(int wait_s) {
    SC_HANDLE scm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return "service stop -> FAILED (no SCM)";

    SC_HANDLE svc = ::OpenServiceW(scm, std::wstring(kService).c_str(),
                                   SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) {
        ::CloseServiceHandle(scm);
        return "service stop -> FAILED (service not found)";
    }

    SERVICE_STATUS sss{};
    const BOOL stopped = ::ControlService(svc, SERVICE_CONTROL_STOP, &sss);
    ::CloseServiceHandle(svc);
    ::CloseServiceHandle(scm);

    if (wait_s > 0) ::Sleep(static_cast<DWORD>(wait_s) * 1000);

    const auto st = service_status();
    return std::string("service stop -> ") + (stopped ? "requested, " : "FAILED, ") +
           str::to_utf8(st.state_text());
}

CloseResult close_gameloop(const CloseOptions& opts) {
    CloseResult res;

    // Required to reach the LocalSystem-spawned mid-match processes. A failure
    // is not fatal: it just means stages 2-3 will skip anything we cannot open,
    // and those get reported in `left`.
    (void)enable_debug_privilege();

    auto live = running();
    if (live.empty()) {
        res.ok = true;
        res.notes.push_back("GameLoop already closed");
        return res;
    }
    res.notes.push_back("closing " + std::to_string(live.size()) + " GameLoop process(es)");

    // Stage 1 — polite WM_CLOSE.
    auto left = graceful_close(opts.grace_wait_s);
    res.notes.push_back("graceful WM_CLOSE: " + std::to_string(left.size()) + " left");

    // Stage 2 — terminate the tree. The native implementation always forces:
    // there is no non-forcing tree kill in Win32, so the Python /T → /F /T
    // pair collapses into one pass, and a second pass runs only if the first
    // failed to take (e.g. processes created between the snapshot and now).
    if (!left.empty()) {
        std::vector<std::uint32_t> pids;
        pids.reserve(left.size());
        for (const auto& p : left) pids.push_back(p.pid);

        left = kill_pids(pids, true, opts.kill_wait_ms);
        res.notes.push_back("terminate tree: " + std::to_string(left.size()) + " left");
    }
    if (!left.empty() && opts.force) {
        std::vector<std::uint32_t> pids;
        pids.reserve(left.size());
        for (const auto& p : left) pids.push_back(p.pid);

        left = kill_pids(pids, true, opts.kill_wait_ms);
        res.notes.push_back("terminate tree (retry): " + std::to_string(left.size()) + " left");
    }

    // Stage 4 — WM_QUIT to the UI threads of anything still standing.
    if (!left.empty()) {
        auto notes = tray_quit_fallback(2);
        res.notes.insert(res.notes.end(), notes.begin(), notes.end());
        left = running();
        if (!left.empty() && opts.force) {
            std::vector<std::uint32_t> pids;
            pids.reserve(left.size());
            for (const auto& p : left) pids.push_back(p.pid);
            left = kill_pids(pids, true, 1000);
            res.notes.push_back("final force sweep: " + std::to_string(left.size()) + " left");
        }
    }

    // Stage 5 — the service, only with explicit consent. GameLoop auto-starts
    // it, so stopping it here changes state beyond the current session.
    if (opts.stop_service_now) {
        res.notes.push_back(stop_service(4));
        left = running();
    }

    res.left = std::move(left);
    res.ok = res.left.empty();
    return res;
}

// ── Priorities ───────────────────────────────────────────────────────────────

std::vector<std::pair<std::wstring, std::wstring>> default_priority_plan() {
    // procs.py's default plan, verbatim.
    return {
        {L"gameloopemulator.exe", std::wstring(kPrioHigh)},
        {L"gameloopvm.exe", std::wstring(kPrioHigh)},
        {L"gameloop.exe", std::wstring(kPrioHigh)},
        {L"gameloopassistant.exe", std::wstring(kPrioAbove)},
        {L"gameloopdldsvr.exe", std::wstring(kPrioBelow)},
    };
}

std::vector<std::string> set_priorities(
    const std::vector<std::pair<std::wstring, std::wstring>>& plan_in) {
    const auto plan = plan_in.empty() ? default_priority_plan() : plan_in;
    std::vector<std::string> notes;

    for (const auto& p : list_gameloop()) {
        const auto name = str::ascii_lower(p.name);

        const std::wstring* want = nullptr;
        for (const auto& [img, prio] : plan) {
            if (str::ascii_lower(img) == name) {
                want = &prio;
                break;
            }
        }
        if (!want) continue;

        // Own-user processes need no admin; elevated ones need the privilege
        // the ladder already enabled.
        HANDLE h = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION, FALSE,
                                 p.pid);
        if (!h) {
            notes.push_back(str::to_utf8(p.name) + "(pid " + std::to_string(p.pid) +
                            "): open denied (elevated?)");
            continue;
        }

        const BOOL ok = ::SetPriorityClass(h, priority_class(*want));
        ::CloseHandle(h);

        notes.push_back(str::to_utf8(p.name) + "(pid " + std::to_string(p.pid) + "): " +
                        str::to_utf8(*want) + (ok ? " ok" : " fail"));
    }

    if (notes.empty()) notes.push_back("no GameLoop processes running");
    return notes;
}

// ── Host ─────────────────────────────────────────────────────────────────────

HostResources host_resources() {
    HostResources out;

    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);
    out.cores = static_cast<int>(si.dwNumberOfProcessors);

    // On a machine with more than 64 logical CPUs, GetSystemInfo reports only
    // the CURRENT processor group's count, and the VM slider would then cap
    // the allocation below what the hardware actually allows.
    //
    // GetActiveProcessorCount(ALL_PROCESSOR_GROUPS, &count) is the two-argument
    // form that spans every group, but it only exists on Windows 11 / Server
    // 2022 SDKs. It is resolved dynamically rather than called directly so the
    // binary still loads on an older Windows 10 host.
    using GetActiveProcessorCountFn = DWORD(WINAPI*)(WORD, DWORD*);
    static const auto fn = [] () -> GetActiveProcessorCountFn {
        HMODULE k32 = ::GetModuleHandleW(L"kernel32.dll");
        if (!k32) return nullptr;
        return reinterpret_cast<GetActiveProcessorCountFn>(reinterpret_cast<void*>(
            ::GetProcAddress(k32, "GetActiveProcessorCount")));
    }();

    if (fn) {
        DWORD total = 0;
        if (fn(ALL_PROCESSOR_GROUPS, &total) && total > 0) {
            out.cores = static_cast<int>(total);
        }
    }

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (::GlobalMemoryStatusEx(&ms)) {
        // Floored whole GB, matching Python's // (1024**3).
        out.mem_gb = static_cast<int>(ms.ullTotalPhys / (1024ull * 1024ull * 1024ull));
    }
    return out;
}

} // namespace glt::procs