#include "glt/core/paths.hpp"

#include "glt/core/strings.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <vector>

namespace glt::paths {
namespace {

constexpr std::wstring_view kTencentTail = L"Tencent\\GameLoop";

// %APPDATA% (FOLDERID_RoamingAppData). Empty when the shell API fails, which
// only happens in a badly broken profile — callers treat "" as "not found".
std::wstring roaming_appdata() {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        return {};
    }
    std::wstring out(raw);
    ::CoTaskMemFree(raw);
    return out;
}

std::wstring env_w(const wchar_t* name) {
    const DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring buf(n, L'\0');
    const DWORD got = ::GetEnvironmentVariableW(name, buf.data(), n);
    buf.resize(got);
    return buf;
}

} // namespace

std::wstring appdata_db() {
    const std::wstring base = roaming_appdata();
    if (base.empty()) return {};

    const auto p = str::path_join(base, L"Tencent\\GameLoop\\db\\Configs.db");
    return str::file_exists(p) ? p : std::wstring{};
}

std::wstring db_dir() {
    const std::wstring db = appdata_db();
    return db.empty() ? std::wstring{} : str::path_parent(db);
}

std::wstring smk_conf() {
    const std::wstring base = roaming_appdata();
    if (base.empty()) return {};

    std::wstring rel = L"Tencent\\GameLoop\\config\\";
    rel += str::to_wide(kPkg);
    rel += L"\\smk.conf";

    const auto p = str::path_join(base, rel);
    return str::file_exists(p) ? p : std::wstring{};
}

std::wstring install_root() {
    std::vector<std::wstring> cands;

    // 1. The ProgramFiles family. ProgramW6432 is the 64-bit view of a
    //    32-bit process, so on x64 all three can differ; check all of them.
    for (const auto* var : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432"}) {
        const std::wstring base = env_w(var);
        if (base.empty()) continue;
        cands.push_back(str::path_join(base, kTencentTail));
    }

    // 2. Every fixed volume, in drive-letter order, both layouts.
    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) continue;
        std::wstring drive(1, static_cast<wchar_t>(L'A' + i));
        drive += L":\\";
        cands.push_back(str::path_join(drive, L"Program Files\\Tencent\\GameLoop"));
        cands.push_back(str::path_join(drive, L"Tencent\\GameLoop"));
    }

    for (const auto& c : cands) {
        if (str::dir_exists(c)) return c;
    }

    // 3. Last resort: the installer records its own path in HKLM. Checking this
    //    after the scan preserves the original preference order — a registry
    //    entry pointing at a stale directory must not shadow a live install.
    return install_root_from_registry();
}

std::wstring install_root_from_registry() {
    // Read directly rather than via glt::reg, to keep paths.hpp dependency-free
    // (registry.hpp includes paths.hpp for its own defaults).
    for (const auto* sub : {L"SOFTWARE\\Tencent\\GameLoop",
                            L"SOFTWARE\\WOW6432Node\\Tencent\\GameLoop"}) {
        HKEY k = nullptr;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) {
            continue;
        }

        // InstallDir / InstallPath / Path — whichever this build happened to write.
        for (const auto* name : {L"InstallDir", L"InstallPath", L"Path"}) {
            DWORD type = 0, cb = 0;
            if (::RegQueryValueExW(k, name, nullptr, &type, nullptr, &cb) != ERROR_SUCCESS) {
                continue;
            }
            if (type != REG_SZ && type != REG_EXPAND_SZ) continue;

            std::wstring val(cb / sizeof(wchar_t), L'\0');
            if (::RegQueryValueExW(k, name, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(val.data()), &cb) != ERROR_SUCCESS) {
                continue;
            }
            val.resize(wcslen(val.c_str()));
            if (!val.empty() && str::dir_exists(val)) {
                ::RegCloseKey(k);
                return val;
            }
        }
        ::RegCloseKey(k);
    }
    return {};
}

std::wstring app_version_dir(std::wstring_view root) {
    std::wstring r(root);
    if (r.empty()) r = install_root();
    if (r.empty()) return {};

    const std::wstring app_dir = str::path_join(r, L"Application");

    // Only "*.*.*" directories qualify: the Application root itself and any
    // non-version subfolder holds just launcher stubs, and picking one of those
    // would make every later path join resolve to nothing.
    std::wstring best;
    for (const auto& name : str::list_dir(app_dir)) {
        const auto dot_count = static_cast<std::size_t>(std::count(name.begin(), name.end(), L'.'));
        if (dot_count < 2) continue;
        if (!str::dir_exists(str::path_join(app_dir, name))) continue;

        if (best.empty() || str::version_less(best, name)) best = name;
    }
    return best.empty() ? std::wstring{} : str::path_join(app_dir, best);
}

std::wstring exe_in_build(std::wstring_view name, std::wstring_view root) {
    const std::wstring dir = app_version_dir(root);
    if (dir.empty()) return {};

    const auto p = str::path_join(dir, name);
    return str::file_exists(p) ? p : std::wstring{};
}

std::wstring launcher_exe(std::wstring_view root) {
    std::wstring r(root);
    if (r.empty()) r = install_root();
    if (r.empty()) return {};

    // 1. Application\GameLoopLauncher.exe
    const auto p1 = str::path_join(r, L"Application\\GameLoopLauncher.exe");
    if (str::file_exists(p1)) return p1;

    // 2. Application\<version>\GameLoop.exe
    const auto p2 = exe_in_build(L"GameLoop.exe", r);
    if (str::file_exists(p2)) return p2;

    // 3. Application\<version>\GameLoopLauncher.exe
    const auto p3 = exe_in_build(L"GameLoopLauncher.exe", r);
    if (str::file_exists(p3)) return p3;

    // 4. AppMarket.exe (older or international builds)
    const auto p4 = exe_in_build(L"AppMarket.exe", r);
    if (str::file_exists(p4)) return p4;

    return {};
}

std::wstring adb_exe(std::wstring_view root) { return exe_in_build(L"adb.exe", root); }

std::wstring ano_tmp_dir() {
    const std::wstring dir = app_version_dir();
    if (dir.empty()) return {};
    const auto p = str::path_join(dir, L"ano_tmp");
    return str::dir_exists(p) ? p : std::wstring{};
}

std::wstring ano_tmp_quarantine_dir() {
    const std::wstring dir = app_version_dir();
    if (dir.empty()) return {};
    return str::path_join(dir, L"ano_tmp_quarantine");
}

HostInfo host_info() {
    // RTL_OSVERSIONINFOEXW, read via ntdll. GetVersionEx is shimmed by the
    // application-compatibility layer and would report 6.2 on Win10/11; RtlGet-
    // Version always returns the truth.
    //
    // The struct is declared locally rather than pulled from a header: it is
    // an internal ntdll type, it is not part of the stable SDK surface, and
    // including <winternl.h> drags in half of the NT namespace.
    struct RtlOsVersionInfoExW {
        ULONG dwOSVersionInfoSize;
        ULONG dwMajorVersion;
        ULONG dwMinorVersion;
        ULONG dwBuildNumber;
        ULONG dwPlatformId;
        WCHAR szCSDVersion[128];
    };
    using RtlGetVersionFn = LONG(WINAPI*)(RtlOsVersionInfoExW*);

    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    HostInfo out;
    if (ntdll) {
        const auto fn = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(::GetProcAddress(ntdll, "RtlGetVersion")));
        if (fn) {
            RtlOsVersionInfoExW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (fn(&info) == 0) {
                wchar_t buf[64]{};
                swprintf_s(buf, L"%lu.%lu.%lu", info.dwMajorVersion, info.dwMinorVersion,
                           info.dwBuildNumber);
                out.release = buf;
                out.version = buf;
                if (info.szCSDVersion[0]) {
                    out.version += L" ";
                    out.version += info.szCSDVersion;
                }
            }
        }
    }

    out.machine = env_w(L"PROCESSOR_ARCHITECTURE");
    if (out.machine.empty()) out.machine = L"unknown";
    return out;
}

} // namespace glt::paths