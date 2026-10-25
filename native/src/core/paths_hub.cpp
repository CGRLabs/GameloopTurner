#include "glt/core/paths_hub.hpp"

#include "glt/core/paths.hpp"
#include "glt/core/strings.hpp"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <vector>

// Hub (TAC) install-root discovery. Split from paths.cpp on purpose: the two
// builds disagree about WHERE to look first, and that decision must not be
// shared, because getting it wrong silently tunes the wrong install.
namespace glt::paths {
namespace {

constexpr std::wstring_view kTencentTail = L"Tencent\\GameLoop";

std::wstring env_w(const wchar_t* name) {
    const DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring buf(n, L'\0');
    const DWORD got = ::GetEnvironmentVariableW(name, buf.data(), n);
    buf.resize(got);
    return buf;
}

// %APPDATA% (FOLDERID_RoamingAppData). The AppData layout is SAME on Hub
// (report A1.2/A1.3), so this matches the Beta implementation byte for byte.
std::wstring roaming_appdata() {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) {
        return {};
    }
    std::wstring out(raw);
    ::CoTaskMemFree(raw);
    return out;
}

// Reads the FIRST install value that exists under the single native branch.
// Deliberately does not touch WOW6432Node (see the header).
std::wstring registry_install_root() {
    HKEY k = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, std::wstring(kHubRegistryKey).c_str(), 0, KEY_READ,
                        &k) != ERROR_SUCCESS) {
        return {};
    }

    std::wstring found;
    for (const auto name : hub_registry_value_names()) {
        DWORD type = 0, cb = 0;
        if (::RegQueryValueExW(k, std::wstring(name).c_str(), nullptr, &type, nullptr, &cb) !=
            ERROR_SUCCESS) {
            continue;
        }
        if (type != REG_SZ && type != REG_EXPAND_SZ) continue;

        std::wstring val(cb / sizeof(wchar_t), L'\0');
        if (::RegQueryValueExW(k, std::wstring(name).c_str(), nullptr, &type,
                               reinterpret_cast<LPBYTE>(val.data()), &cb) != ERROR_SUCCESS) {
            continue;
        }
        val.resize(wcslen(val.c_str()));
        if (!val.empty() && str::dir_exists(val)) {
            found = val;
            break;
        }
    }
    ::RegCloseKey(k);
    return found;
}

} // namespace

std::vector<std::wstring_view> hub_registry_value_names() {
    // InstallPath first: that is the value Hub actually writes.
    return {L"InstallPath", L"InstallDir", L"Path"};
}

std::vector<std::wstring_view> hub_registry_subkeys() {
    // Native 64-bit branch ONLY. The WOW6432Node mirror is absent on purpose.
    return {kHubRegistryKey};
}

std::vector<std::wstring_view> hub_drive_scan_tails() {
    return {L"Program Files\\Tencent\\GameLoop", L"Tencent\\GameLoop"};
}

std::vector<std::wstring_view> hub_launcher_names() {
    // AppMarket.exe is ABSENT in Hub (report A1.7) and must not be assumed.
    return {L"GameLoopLauncher.exe", L"GameLoop.exe"};
}

// ── Public API implemented for the Hub build ─────────────────────────────────

// AppData accessors. The DB and smk.conf paths are SAME on Hub (report
// A1.2/A1.3), so these are the Beta logic, duplicated for build isolation.
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

std::wstring install_root_from_registry() { return registry_install_root(); }

std::wstring install_root() {
    // Registry FIRST — the whole point of the Hub build. A live install found
    // via InstallPath (C:\ARENA\GameLoop) wins over anything the drive scan
    // might stumble onto.
    if (const std::wstring reg = install_root_from_registry(); !reg.empty()) {
        return reg;
    }

    // Fallback: the ProgramFiles family, then the fixed-drive scan. This keeps
    // working for a user who moved the install out of C:\ARENA\GameLoop and
    // whose registry entry is stale.
    std::vector<std::wstring> cands;

    for (const auto* var : {L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432"}) {
        const std::wstring base = env_w(var);
        if (base.empty()) continue;
        cands.push_back(str::path_join(base, kTencentTail));
    }

    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) continue;
        std::wstring drive(1, static_cast<wchar_t>(L'A' + i));
        drive += L":\\";
        for (const auto tail : hub_drive_scan_tails()) {
            cands.push_back(str::path_join(drive, tail));
        }
    }

    for (const auto& c : cands) {
        if (str::dir_exists(c)) return c;
    }
    return {};
}

// launcher_exe / app_version_dir / adb_exe / ano_tmp_dir / ano_tmp_quarantine_dir
// all keep the Beta shape: only the discovery of the ROOT changes for Hub. They
// are re-implemented here (not shared) so the Hub build has its own copy, per
// the v1 "duplicate over #ifdef" strategy.

std::wstring app_version_dir(std::wstring_view root) {
    std::wstring r(root);
    if (r.empty()) r = install_root();
    if (r.empty()) return {};

    const std::wstring app_dir = str::path_join(r, L"Application");

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

    // 1. Application\GameLoopLauncher.exe (Hub ships a copy at the root).
    const auto p1 = str::path_join(r, L"Application\\GameLoopLauncher.exe");
    if (str::file_exists(p1)) return p1;

    // 2..n the accepted launcher names inside the version dir. No AppMarket.
    for (const auto name : hub_launcher_names()) {
        const auto p = exe_in_build(name, r);
        if (str::file_exists(p)) return p;
    }
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
    // ano_tmp_quarantine is ABSENT in Hub (report A1.9). The path is still
    // returned so quarantine() can create it on first use; the absence is
    // handled by the caller, not by pretending the directory exists.
    const std::wstring dir = app_version_dir();
    if (dir.empty()) return {};
    return str::path_join(dir, L"ano_tmp_quarantine");
}

// ── Host info: identical to Beta, duplicated for build isolation ─────────────

HostInfo host_info() {
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