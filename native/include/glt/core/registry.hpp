// ─────────────────────────────────────────────────────────────────────────────
// glt/core/registry.hpp — HKCU GameLoop settings.
//
// Ported from legacy-python/src/core/registry.py.
//
// WHERE THE VALUES ACTUALLY LIVE (verified on this machine)
// -----------------------------------------------------------
//   HKEY_CURRENT_USER
//     \Software\Tencent\GameLoop                    ← BASE, a container only
//     \Software\Tencent\GameLoop\Settings\Common    ← COMMON, the values here
//
// There is NO \...\GameLoop\Performance key. An earlier draft of the port spec
// named one, and writing there would have been a silent no-op — RegSetValueExW
// on a non-existent key fails, but a caller that ignores the return code sees
// "success" and the user sees nothing change.
//
// Value map, all REG_QWORD on write:
//   PerformanceCpuCoreNum  ≤ host_cores − 1   (written)
//   PerformanceMemoryGb     ≤ host_mem_gb − 2  (written)
//   PerformanceMode                          (read-only display)
//   RenderMode           0=Auto 1=OpenGL+ 2=DirectX+ 3=Vulkan  (display only)
//   RenderUserType       0=never overridden 1=manual           (display only)
//
// RenderMode / RenderUserType are NEVER written. RenderMode is the engine's
// per-map backend auto-selection; forcing it fights that selection, and
// RenderUserType is the "the user overrode this" marker.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace glt::reg {

inline constexpr std::wstring_view kBase = L"Software\\Tencent\\GameLoop";
inline constexpr std::wstring_view kCommon =
    L"Software\\Tencent\\GameLoop\\Settings\\Common";

// Value names.
inline constexpr std::wstring_view kPerfMode = L"PerformanceMode";
inline constexpr std::wstring_view kPerfMemGb = L"PerformanceMemoryGb";
inline constexpr std::wstring_view kPerfCores = L"PerformanceCpuCoreNum";
inline constexpr std::wstring_view kRenderMode = L"RenderMode";
inline constexpr std::wstring_view kRenderUserType = L"RenderUserType";

inline constexpr std::wstring_view kSettingsLauncher =
    L"Software\\Tencent\\GameLoop\\Settings\\GameLoop";

// ── Values ───────────────────────────────────────────────────────────────────

// Registry values are polymorphic; GameLoop uses QWORD but a user could have
// DWORD from an older build. Absent → nullopt.
using RegValue = std::optional<std::int64_t>;

// Every value under `path`. Missing keys yield an empty map, never an error.
[[nodiscard]] std::vector<std::pair<std::wstring, RegValue>> read_all(std::wstring_view path);

// Direct subkey names.
[[nodiscard]] std::vector<std::wstring> subkeys(std::wstring_view path);

[[nodiscard]] RegValue read_qword(std::wstring_view path, std::wstring_view value_name);

// Writes a REG_QWORD. Returns false on failure (key missing, access denied).
[[nodiscard]] bool write_qword(std::wstring_view path, std::wstring_view value_name,
                               std::int64_t value);

// Creates the key path if needed, then writes. Used by set_perf() so a user who
// has never opened GameLoop's advanced settings still gets the values.
[[nodiscard]] bool write_qword_create(std::wstring_view path, std::wstring_view value_name,
                                      std::int64_t value);

// ── Allocation ───────────────────────────────────────────────────────────────

struct PerfAllocation {
    RegValue performance_mode;
    RegValue memory_gb;
    RegValue cpu_cores;
    RegValue render_mode;
    RegValue render_user_type;

    [[nodiscard]] bool empty() const {
        return !performance_mode && !memory_gb && !cpu_cores && !render_mode &&
               !render_user_type;
    }
};

[[nodiscard]] PerfAllocation perf_allocation();

// ── Render-mode labels ───────────────────────────────────────────────────────

// 0 = Auto → the engine picks OpenGL+/DirectX+ per launched game (the vendor
// path). 1/2 force a backend, 3 = Vulkan on newer builds.
// Returns "unknown (N)" for a value outside the table, matching registry.py.
[[nodiscard]] std::wstring render_mode_label(const RegValue& v);
[[nodiscard]] std::wstring render_user_label(const RegValue& v);

// ── Guarded write ────────────────────────────────────────────────────────────

struct HostResources {
    int cores = 0;
    int mem_gb = 0;
};

// Throws std::runtime_error when the request would starve Windows:
//   cores > host_cores − 1  or  mem_gb > host_mem_gb − 2
// The Python build raised ValueError with exactly these messages, and the UI
// surfaces the text, so the wording is preserved.
[[nodiscard]] std::vector<std::string> set_perf(int cores, int mem_gb,
                                                const HostResources& host);

// ── Backup / restore ─────────────────────────────────────────────────────────

// Recursive {subkey path → {value name → value}} snapshot, JSON-ready.
[[nodiscard]] std::vector<std::pair<std::wstring, std::vector<std::pair<std::wstring, RegValue>>>>
export_branch(std::wstring_view path);

} // namespace glt::reg