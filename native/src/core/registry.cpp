#include "glt/core/registry.hpp"

#include "glt/core/log.hpp"

#include <windows.h>

#include <cstring>
#include <stdexcept>

namespace glt::reg {
namespace {

// HKEY_CURRENT_USER with read / read+write, tolerant of a missing key.
HKEY open_key(std::wstring_view path, bool write) {
    HKEY k = nullptr;
    const REGSAM sam = KEY_READ | (write ? KEY_SET_VALUE : 0);
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, std::wstring(path).c_str(), 0, sam, &k) !=
        ERROR_SUCCESS) {
        return nullptr;
    }
    return k;
}

// Creates every component of `path` under HKCU.
bool create_key(std::wstring_view path) {
    const std::wstring p(path);
    if (p.empty()) return false;

    HKEY k = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, p.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                          KEY_READ | KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    ::RegCloseKey(k);
    return true;
}

// Walks every value under an open key, decoding the type generically. A key
// can hold a mix of DWORD / QWORD / SZ on different GameLoop builds, so the
// caller gets "whatever number this was" rather than a typed struct.
bool read_all_open(HKEY k, std::vector<std::pair<std::wstring, RegValue>>& out) {
    for (DWORD i = 0;; ++i) {
        wchar_t name[512]{};
        DWORD name_cb = static_cast<DWORD>(sizeof(name));
        DWORD type = 0;
        DWORD data_cb = 0;

        if (::RegEnumValueW(k, i, name, &name_cb, nullptr, &type, nullptr, &data_cb) !=
            ERROR_SUCCESS) {
            break; // ERROR_NO_MORE_ITEMS, or an unreadable value — stop cleanly
        }

        std::vector<BYTE> data(data_cb);
        if (data_cb > 0) {
            DWORD got = data_cb;
            if (::RegQueryValueExW(k, name, nullptr, &type, data.data(), &got) !=
                ERROR_SUCCESS) {
                continue;
            }
            data_cb = got;
        }

        std::int64_t v = 0;
        switch (type) {
            case REG_QWORD:
                if (data_cb >= 8) {
                    std::uint64_t u = 0;
                    std::memcpy(&u, data.data(), 8);
                    v = static_cast<std::int64_t>(u);
                }
                break;
            case REG_DWORD:
                if (data_cb >= 4) {
                    DWORD d = 0;
                    std::memcpy(&d, data.data(), 4);
                    v = static_cast<std::int64_t>(d);
                }
                break;
            case REG_DWORD_BIG_ENDIAN:
                if (data_cb >= 4) {
                    const BYTE* p = data.data();
                    v = static_cast<std::int64_t>(
                        (static_cast<std::uint32_t>(p[0]) << 24) |
                        (static_cast<std::uint32_t>(p[1]) << 16) |
                        (static_cast<std::uint32_t>(p[2]) << 8) |
                        static_cast<std::uint32_t>(p[3]));
                }
                break;
            case REG_SZ:
            case REG_EXPAND_SZ: {
                const std::wstring s(reinterpret_cast<const wchar_t*>(data.data()));
                try {
                    v = std::stoll(s);
                } catch (...) {
                    v = 0;
                }
                break;
            }
            default:
                continue; // binary blobs are not settings we expose
        }

        // RegEnumValueW returns the name length including the NUL on success.
        std::wstring name_str(name);
        if (!name_str.empty() && name_str.back() == L'\0') name_str.pop_back();
        out.emplace_back(std::move(name_str), v);
    }
    return true;
}

} // namespace

std::vector<std::pair<std::wstring, RegValue>> read_all(std::wstring_view path) {
    std::vector<std::pair<std::wstring, RegValue>> out;

    HKEY k = open_key(path, false);
    if (!k) return out; // FileNotFoundError in the Python build → empty dict

    read_all_open(k, out);
    ::RegCloseKey(k);
    return out;
}

std::vector<std::wstring> subkeys(std::wstring_view path) {
    std::vector<std::wstring> out;

    HKEY k = open_key(path, false);
    if (!k) return out;

    for (DWORD i = 0;; ++i) {
        wchar_t name[512]{};
        DWORD name_cb = static_cast<DWORD>(sizeof(name));
        if (::RegEnumKeyExW(k, i, name, &name_cb, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS) {
            break;
        }
        std::wstring s(name);
        if (!s.empty() && s.back() == L'\0') s.pop_back();
        out.push_back(std::move(s));
    }

    ::RegCloseKey(k);
    return out;
}

RegValue read_qword(std::wstring_view path, std::wstring_view value_name) {
    HKEY k = open_key(path, false);
    if (!k) return std::nullopt;

    DWORD type = 0, cb = sizeof(std::int64_t);
    std::int64_t v = 0;
    const LSTATUS rc = ::RegQueryValueExW(k, std::wstring(value_name).c_str(), nullptr, &type,
                                          reinterpret_cast<LPBYTE>(&v), &cb);
    ::RegCloseKey(k);

    // Accept DWORD/QWORD interchangeably: an older GameLoop wrote DWORD here.
    if (rc != ERROR_SUCCESS || (type != REG_QWORD && type != REG_DWORD)) {
        // Last resort: the value may have been stored as a decimal string.
        for (const auto& [name, val] : read_all(path)) {
            if (name == value_name) return val;
        }
        return std::nullopt;
    }
    return v;
}

bool write_qword(std::wstring_view path, std::wstring_view value_name, std::int64_t value) {
    HKEY k = open_key(path, true);
    if (!k) return false;

    const DWORD cb = sizeof(std::int64_t);
    const LSTATUS rc = ::RegSetValueExW(k, std::wstring(value_name).c_str(), 0, REG_QWORD,
                                        reinterpret_cast<const BYTE*>(&value), cb);
    ::RegCloseKey(k);
    return rc == ERROR_SUCCESS;
}

bool write_qword_create(std::wstring_view path, std::wstring_view value_name,
                        std::int64_t value) {
    if (!open_key(path, false) && !create_key(path)) return false;
    return write_qword(path, value_name, value);
}

PerfAllocation perf_allocation() {
    PerfAllocation a;
    a.performance_mode = read_qword(kCommon, kPerfMode);
    a.memory_gb = read_qword(kCommon, kPerfMemGb);
    a.cpu_cores = read_qword(kCommon, kPerfCores);
    a.render_mode = read_qword(kCommon, kRenderMode);
    a.render_user_type = read_qword(kCommon, kRenderUserType);
    return a;
}

std::wstring render_mode_label(const RegValue& v) {
    if (!v) return L"unset";
    switch (*v) {
        case 0: return L"Auto (engine picks per game)";
        case 1: return L"OpenGL+";
        case 2: return L"DirectX+";
        case 3: return L"Vulkan";
        default: return L"unknown (" + std::to_wstring(*v) + L")";
    }
}

std::wstring render_user_label(const RegValue& v) {
    if (!v) return L"unset";
    switch (*v) {
        case 0: return L"Auto (never overridden)";
        case 1: return L"Manually overridden";
        default: return L"unknown (" + std::to_wstring(*v) + L")";
    }
}

std::vector<std::string> set_perf(int cores, int mem_gb, const HostResources& host) {
    // Refuse allocations that leave nothing for Windows. registry.py raised
    // ValueError with these messages and the UI printed them verbatim.
    if (host.cores > 0 && cores > host.cores - 1) {
        throw std::runtime_error("cores " + std::to_string(cores) +
                                 " leaves no CPU for Windows (host " +
                                 std::to_string(host.cores) + ")");
    }
    if (host.mem_gb > 0 && mem_gb > host.mem_gb - 2) {
        throw std::runtime_error("mem " + std::to_string(mem_gb) +
                                 "GB leaves no RAM for Windows (host " +
                                 std::to_string(host.mem_gb) + "GB)");
    }

    std::vector<std::string> notes;
    if (write_qword_create(kCommon, kPerfCores, cores)) {
        notes.push_back("PerformanceCpuCoreNum=" + std::to_string(cores));
    } else {
        notes.push_back("PerformanceCpuCoreNum FAILED");
        log::error("registry write PerformanceCpuCoreNum failed");
    }
    if (write_qword_create(kCommon, kPerfMemGb, mem_gb)) {
        notes.push_back("PerformanceMemoryGb=" + std::to_string(mem_gb));
    } else {
        notes.push_back("PerformanceMemoryGb FAILED");
        log::error("registry write PerformanceMemoryGb failed");
    }
    return notes;
}

std::vector<std::pair<std::wstring, std::vector<std::pair<std::wstring, RegValue>>>>
export_branch(std::wstring_view path) {
    std::vector<std::pair<std::wstring, std::vector<std::pair<std::wstring, RegValue>>>> out;
    out.emplace_back(std::wstring(path), read_all(path));

    for (const auto& sub : subkeys(path)) {
        const std::wstring child = std::wstring(path) + L"\\" + sub;
        auto nested = export_branch(child);
        out.insert(out.end(), nested.begin(), nested.end());
    }
    return out;
}

} // namespace glt::reg