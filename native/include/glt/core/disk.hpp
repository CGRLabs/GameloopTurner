// ─────────────────────────────────────────────────────────────────────────────
// glt/core/disk.hpp — install footprint and free-space gauge.
//
// Ported from legacy-python/src/core/disk.py.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>

namespace glt::disk {

// The engine needs headroom for shader cache, unpacked APK assets and the
// ano_tmp template pack. Below this the emulator fails to launch in ways that
// look like a corrupt install.
inline constexpr double kEngineMinFreeGb = 10.0;

// Total bytes under `path`, recursive. Symlinks/junctions are followed at most
// once per path to avoid looping on a reparse point.
[[nodiscard]] std::uint64_t dir_size(const std::wstring& path);

// {label → bytes} for the install root, its GameLoop / GameLoopData
// subfolders, and %APPDATA%\Tencent.
[[nodiscard]] std::vector<std::pair<std::string, std::uint64_t>> footprint(
    const std::wstring& install_root);

struct DriveInfo {
    std::wstring drive;      // "C:\"
    std::uint64_t total = 0;
    std::uint64_t free = 0;
};

[[nodiscard]] std::vector<DriveInfo> drives();

struct DriveStatus {
    std::wstring drive;
    double free_gb = -1.0;
    double min_gb = kEngineMinFreeGb;
    bool ok = false;
};

// Free space on the volume holding `install_root` (falling back to the volume
// holding %APPDATA% when the install cannot be located).
[[nodiscard]] DriveStatus game_drive_status(const std::wstring& install_root);

} // namespace glt::disk