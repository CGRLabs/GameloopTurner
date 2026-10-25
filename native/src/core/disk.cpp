#include "glt/core/disk.hpp"

#include "glt/core/paths.hpp"
#include "glt/core/strings.hpp"

#include <windows.h>
#include <shlobj.h>

#include <set>

namespace glt::disk {
namespace {

std::uint64_t dir_size_impl(const std::wstring& path, std::set<std::wstring>& seen) {
    // Guard against reparse-point loops: a junction back up the tree would
    // otherwise make this recurse forever.
    const std::wstring key = str::ascii_lower(path);
    if (!seen.insert(key).second) return 0;

    std::uint64_t total = 0;
    WIN32_FIND_DATAW fd{};

    const std::wstring pattern = str::path_join(path, L"*");
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;

        const std::wstring child = str::path_join(path, name);
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            total += dir_size_impl(child, seen);
        } else if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
            total += (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        }
    } while (::FindNextFileW(h, &fd));

    ::CloseHandle(h);
    return total;
}

std::wstring roaming_appdata() {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw))) return {};
    std::wstring out(raw);
    ::CoTaskMemFree(raw);
    return out;
}

} // namespace

std::uint64_t dir_size(const std::wstring& path) {
    if (path.empty() || !str::dir_exists(path)) return 0;
    std::set<std::wstring> seen;
    return dir_size_impl(path, seen);
}

std::vector<std::pair<std::string, std::uint64_t>> footprint(const std::wstring& install_root) {
    std::vector<std::pair<std::string, std::uint64_t>> out;
    if (install_root.empty()) return out;

    // The Python build labelled the install root itself as "Tencent", which is
    // what the directory actually is regardless of where the user pointed the
    // tuner. Preserved so the report reads the same.
    for (const auto* sub : {L"", L"GameLoop", L"GameLoopData"}) {
        const std::wstring p =
            (sub[0] == L'\0') ? install_root : str::path_join(install_root, sub);
        if (!str::dir_exists(p)) continue;

        const std::wstring label = (sub[0] == L'\0') ? L"Tencent" : sub;
        out.emplace_back(str::to_utf8(label), dir_size(p));
    }

    const std::wstring appdata = str::path_join(roaming_appdata(), L"Tencent");
    if (str::dir_exists(appdata)) {
        out.emplace_back("AppData/Tencent", dir_size(appdata));
    }
    return out;
}

std::vector<DriveInfo> drives() {
    std::vector<DriveInfo> out;

    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) continue;

        std::wstring root(1, static_cast<wchar_t>(L'A' + i));
        root += L":\\";

        // Skip optical / ramdisk volumes; GetDiskFreeSpaceEx fails on them and
        // they are never the GameLoop install target.
        const UINT type = ::GetDriveTypeW(root.c_str());
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE && type != DRIVE_REMOTE) continue;

        ULARGE_INTEGER avail{}, total{}, total_free{};
        if (!::GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &total_free)) continue;

        DriveInfo d;
        d.drive = root;
        d.total = total.QuadPart;
        d.free = avail.QuadPart;
        out.push_back(std::move(d));
    }
    return out;
}

DriveStatus game_drive_status(const std::wstring& install_root) {
    DriveStatus out;

    std::wstring probe = install_root;
    if (probe.empty()) probe = roaming_appdata();
    if (probe.empty()) {
        // Nothing to probe — report "unknown" rather than claiming 0 GB free.
        out.free_gb = -1.0;
        return out;
    }

    const std::wstring root = str::path_root(probe);
    out.drive = root.empty() ? probe.substr(0, 3) : root + L"\\";

    std::uint64_t free = 0;
    if (!str::disk_free_bytes(out.drive, free)) {
        out.free_gb = -1.0;
        return out;
    }

    // 1e9, not 2^30: the Python build reported shutil.disk_usage bytes divided
    // by 1e9, so the "10 GB minimum" gate has always been decimal gigabytes.
    // Changing it would silently shift the threshold on every existing user.
    out.free_gb = static_cast<double>(free) / 1e9;
    out.ok = out.free_gb >= out.min_gb;
    return out;
}

} // namespace glt::disk