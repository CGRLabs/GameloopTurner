// Hub (TAC) process control. Duplicated from procs.cpp on purpose (v1 strategy:
// separate implementations even where logic is identical) so the Hub build's
// process list and kill order can diverge without a single #ifdef in a file
// that also serves Beta.
#include "glt/core/procs.hpp"

#include "glt/core/procs_hub.hpp"

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

std::vector<std::wstring_view> hub_extra_game_images() {
    // Lowercase. The `gameloop*` prefix rule already covers GameLoopVfs.exe, but
    // it is listed explicitly so the intent (and the test) is unambiguous.
    return {L"glaboxheadless.exe", L"glaboxsvc.exe", L"acekeeper.exe",
            L"acekeeperservice.exe", L"gameloopvfs.exe"};
}

std::vector<std::wstring_view> hub_kill_order() {
    // Setup.json new-image order first (report §9), then the existing core
    // images in their Beta relative order. GameLoopVfs is NOT repeated in the
    // tail because Setup.json places it before GameLoopVm.
    return {
        L"glaboxheadless.exe",     L"glaboxsvc.exe",
        L"acekeeper.exe",          L"acekeeperservice.exe",
        L"gameloopvfs.exe",        L"gameloopvm.exe",
        L"gameloopemulator.exe",   L"gameloopassistant.exe",
        L"gameloopassistanttoast.exe", L"gameloopdldsvr.exe",
        L"gameloopupdate.exe",     L"gameloopinstaller.exe",
        L"cefrendererprocess.exe", L"gameloop.exe",
        L"gamelooplauncher.exe",
    };
}

std::vector<std::pair<std::wstring, std::wstring>> hub_priority_plan() {
    // Beta's plan plus the Hub images. The new images' priorities are a
    // best-effort starting point marked needs-live-test in the report: the
    // headless game box rides High like the VM, AceKeeper (anti-cheat keeper)
    // Above so it cannot be starved, the service/helpers Normal, the VFS layer
    // Below so file-system work cannot steal a core mid-match.
    return {
        {L"gameloopemulator.exe", std::wstring(kPrioHigh)},
        {L"gameloopvm.exe", std::wstring(kPrioHigh)},
        {L"glaboxheadless.exe", std::wstring(kPrioHigh)},
        {L"gameloop.exe", std::wstring(kPrioHigh)},
        {L"gameloopassistant.exe", std::wstring(kPrioAbove)},
        {L"acekeeper.exe", std::wstring(kPrioAbove)},
        {L"glaboxsvc.exe", std::wstring(kPrioNormal)},
        {L"acekeeperservice.exe", std::wstring(kPrioNormal)},
        {L"gameloopdldsvr.exe", std::wstring(kPrioBelow)},
        {L"gameloopvfs.exe", std::wstring(kPrioBelow)},
    };
}

namespace {

const std::vector<std::wstring_view>& kill_order() {
    static const std::vector<std::wstring_view> kOrder = hub_kill_order();
    return kOrder;
}

int order_key(std::wstring_view name) {
    const auto lower = str::ascii_lower(name);
    const auto& order = kill_order();
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (lower == order[i]) return static_cast<int>(i);
    }
    return static_cast<int>(order.size()); // unknown → last
}

DWORD priority_class(std::wstring_view name) {
    if (name == kPrioHigh)   return HIGH_PRIORITY_CLASS;         // 0x80
    if (name == kPrioAbove)  return ABOVE_NORMAL_PRIORITY_CLASS; // 0x8000
    if (name == kPrioBelow)  return BELOW_NORMAL_PRIORITY_CLASS; // 0x4000
    return NORMAL_PRIORITY_CLASS;                                // 0x20
}

std::wstring self_image() {
    static const std::wstring name = [] {
        wchar_t buf[MAX_PATH]{};
        const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return str::ascii_lower(std::wstring_view(buf, n));
    }();
    return name;
}

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

std::uint64_t working_set_kb(std::uint32_t pid) {
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;

    PROCESS_MEMORY_COUNTERS pmc{};
    const BOOL ok = ::GetProcessMemoryInfo(h, &pmc, sizeof(pmc));
    ::CloseHandle(h);
    if (!ok) return 0;
    return pmc.WorkingSetSize / 1024;
}

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

    // Never report or kill the tuner itself.
    if (lower.find(L"tuner") != std::wstring::npos) return false;
    if (lower.find(L"glt") != std::wstring::npos) return false;
    if (lower == self_image()) return false;

    // GameLoop* (incl. GameLoopVfs) is caught by the prefix rule.
    if (lower.rfind(L"gameloop", 0) == 0) return true;

    // Hub images that do NOT start with "gameloop", plus the shared known set.
    for (const auto img : hub_extra_game_images()) {
        if (lower == img) return true;
    }

    constexpr std::wstring_view kKnown[] = {
        L"cefrendererprocess.exe",
        L"appmarket.exe", // not present in Hub; harmless to recognise
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
            if (start_type == 0x0002) return L"AUTO_START (delayed)";
            return L"START_TYPE_" + std::to_wstring(start_type);
    }
}

// ── Privilege ────────────────────────────────────────────────────────────────

bool enable_debug_privilege() {
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

    EnumCtx ctx;
    ctx.pids = &pids;
    {
        EnumGuard guard(&ctx);
        ::EnumWindows(&enum_window_cb, 0);
    }

    for (HWND h : ctx.windows) {
        ::PostMessageW(h, WM_CLOSE, 0, 0);
    }

    if (wait_s > 0) ::Sleep(static_cast<DWORD>(wait_s) * 1000);
    return running();
}

std::vector<ProcInfo> kill_pids(const std::vector<std::uint32_t>& pids, bool force,
                                int wait_ms) {
    if (pids.empty()) return running();

    const auto all = snapshot_all();

    std::map<std::uint32_t, const ProcInfo*> by_pid;
    for (const auto& p : all) by_pid.emplace(p.pid, &p);

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
        if (!h) continue;

        ::TerminateProcess(h, 1);
        open_handles.push_back(h);
    }

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

    (void)enable_debug_privilege();

    auto live = running();
    if (live.empty()) {
        res.ok = true;
        res.notes.push_back("GameLoop already closed");
        return res;
    }
    res.notes.push_back("closing " + std::to_string(live.size()) + " GameLoop process(es)");

    auto left = graceful_close(opts.grace_wait_s);
    res.notes.push_back("graceful WM_CLOSE: " + std::to_string(left.size()) + " left");

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
    return hub_priority_plan();
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

    using GetActiveProcessorCountFn = DWORD(WINAPI*)(WORD, DWORD*);
    static const auto fn = []() -> GetActiveProcessorCountFn {
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
        out.mem_gb = static_cast<int>(ms.ullTotalPhys / (1024ull * 1024ull * 1024ull));
    }
    return out;
}

} // namespace glt::procs