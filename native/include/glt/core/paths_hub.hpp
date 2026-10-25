// ─────────────────────────────────────────────────────────────────────────────
// glt/core/paths_hub.hpp — Hub (TAC) install-root discovery.
//
// Hub-specific companion to paths.hpp. The Hub build resolves the install root
// in a DIFFERENT order from Beta (docs/targets/hub/PLAN.md §1 and §9):
//
//   1. HKLM\SOFTWARE\Tencent\GameLoop\InstallPath  — the NATIVE 64-bit branch.
//      Hub is installed at C:\ARENA\GameLoop, which the fixed-drive scan does
//      not reach, and the WOW6432Node mirror carries NO install value in Hub,
//      so it must not be required for success.
//   2. The fixed-drive / ProgramFiles scan, as a fallback for a moved install.
//
// The declarations here exist so Hub-specific discovery is testable without a
// live install: they are pure constants and candidate builders, not probe code.
// Nothing in this header is used by the Beta build.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace glt::paths {

// The one registry subkey the Hub build reads. Native 64-bit only: relying on
// the WOW6432Node mirror would produce "not found" on every Hub machine.
inline constexpr std::wstring_view kHubRegistryKey = L"SOFTWARE\\Tencent\\GameLoop";

// The WOW6432Node mirror is intentionally NOT consulted in Hub. Named here so a
// test can prove it is excluded rather than merely forgotten.
inline constexpr std::wstring_view kHubWow64RegistryKey =
    L"SOFTWARE\\WOW6432Node\\Tencent\\GameLoop";

// Install-value names, in the order the Hub build tries them. Hub writes
// InstallPath; InstallDir/Path are kept for builds that wrote the older names.
[[nodiscard]] std::vector<std::wstring_view> hub_registry_value_names();

// Registry subkeys in preference order — exactly one, the native branch.
[[nodiscard]] std::vector<std::wstring_view> hub_registry_subkeys();

// The `<drive>\...` tails the fixed-drive fallback scan appends, in order.
[[nodiscard]] std::vector<std::wstring_view> hub_drive_scan_tails();

// Launcher executable names the Hub build accepts, in preference order.
// AppMarket.exe is ABSENT in Hub and is deliberately not in this list.
[[nodiscard]] std::vector<std::wstring_view> hub_launcher_names();

} // namespace glt::paths