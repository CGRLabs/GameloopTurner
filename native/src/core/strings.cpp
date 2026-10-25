#include "glt/core/strings.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace glt::str {
namespace {

// The identifiers GameLoop treats as per-device secrets. Ported verbatim from
// log.py::SECRET_KEYS — dropping one here would put a device ID in a report the
// user pastes into a public Discord thread.
constexpr std::string_view kSecretKeys[] = {
    "seed_id",   "vdid",       "oaid",          "ams_union_id",  "cert_hash",
    "VPoDioa",   "VOPoDioa",   "VWiDioa",
};

[[nodiscard]] bool is_secret_key(std::string_view key) {
    for (auto k : kSecretKeys) {
        if (iequals(key, k)) return true;
    }
    return false;
}

} // namespace

// ── Encoding ─────────────────────────────────────────────────────────────────

std::wstring to_wide(std::string_view utf8) {
    if (utf8.empty()) return {};

    const int need = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (need <= 0) {
        // Malformed input: fall back to a lossy byte-per-character widening so
        // the caller still gets a usable (if imperfect) path.
        std::wstring out;
        out.reserve(utf8.size());
        for (char c : utf8) {
            out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        }
        return out;
    }

    std::wstring out(static_cast<std::size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                          out.data(), need);
    return out;
}

std::string to_utf8(std::wstring_view utf16) {
    if (utf16.empty()) return {};

    const int need = ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(),
                                           static_cast<int>(utf16.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (need <= 0) return {};

    std::string out(static_cast<std::size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()),
                          out.data(), need, nullptr, nullptr);
    return out;
}

bool is_valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t extra = 0;
        if (c < 0x80) {
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= s.size()) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

// ── Case ─────────────────────────────────────────────────────────────────────

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::wstring ascii_lower(std::wstring_view s) {
    std::wstring out(s);
    std::ranges::transform(out, out.begin(), [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    });
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

bool iequals(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto ca = a[i];
        auto cb = b[i];
        if (ca >= L'A' && ca <= L'Z') ca = static_cast<wchar_t>(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = static_cast<wchar_t>(cb - L'A' + L'a');
        if (ca != cb) return false;
    }
    return true;
}

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;

    const auto h = ascii_lower(haystack);
    const auto n = ascii_lower(needle);
    return h.find(n) != std::string::npos;
}

// ── Paths ────────────────────────────────────────────────────────────────────

std::wstring path_join(std::wstring_view head, std::wstring_view tail) {
    if (tail.empty()) return std::wstring(head);
    // A rooted tail (C:\..., \foo, or a UNC prefix) supersedes the head entirely.
    if (tail.size() >= 2 && tail[1] == L':') return std::wstring(tail);
    if (!tail.empty() && (tail[0] == L'\\' || tail[0] == L'/')) return std::wstring(tail);

    if (head.empty()) return std::wstring(tail);

    std::wstring out(head);
    const wchar_t last = out.back();
    if (last != L'\\' && last != L'/') out.push_back(L'\\');
    out.append(tail);
    return out;
}

std::wstring path_parent(std::wstring_view p) {
    const auto pos = p.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos) return {};
    if (pos == 0) return std::wstring(p.substr(0, 1)); // "\foo" → "\"
    return std::wstring(p.substr(0, pos));
}

std::wstring path_filename(std::wstring_view p) {
    const auto pos = p.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos) return std::wstring(p);
    return std::wstring(p.substr(pos + 1));
}

std::wstring path_stem(std::wstring_view p) {
    const auto name = path_filename(p);
    const auto dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) return name;
    return name.substr(0, dot);
}

std::wstring path_extension(std::wstring_view p) {
    const auto name = path_filename(p);
    const auto dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0) return {};
    return name.substr(dot);
}

std::wstring path_root(std::wstring_view p) {
    // substr() returns a string_view; the explicit construction is what makes
    // this convertible to the wstring return type.
    if (p.size() >= 2 && p[1] == L':') return std::wstring(p.substr(0, 2));
    return {};
}

bool file_exists(std::wstring_view p) {
    if (p.empty()) return false;
    const DWORD attr = ::GetFileAttributesW(std::wstring(p).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool dir_exists(std::wstring_view p) {
    if (p.empty()) return false;
    const DWORD attr = ::GetFileAttributesW(std::wstring(p).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::uint64_t file_size(std::wstring_view p) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!::GetFileAttributesExW(std::wstring(p).c_str(), GetFileExInfoStandard, &fad)) {
        return 0;
    }
    return (static_cast<std::uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
}

bool disk_free_bytes(std::wstring_view p, std::uint64_t& out) {
    ULARGE_INTEGER avail{};
    if (!::GetDiskFreeSpaceExW(std::wstring(p).c_str(), &avail, nullptr, nullptr)) {
        return false;
    }
    out = avail.QuadPart;
    return true;
}

std::vector<std::wstring> list_dir(std::wstring_view dir) {
    std::vector<std::wstring> out;
    const std::wstring pattern = path_join(dir, L"*");
    WIN32_FIND_DATAW fd{};

    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;

    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        out.push_back(name);
    } while (::FindNextFileW(h, &fd));

    ::FindClose(h);
    return out;
}

std::vector<std::wstring> glob(std::wstring_view dir, std::wstring_view pattern) {
    std::vector<std::wstring> out;
    const auto pat = ascii_lower(pattern);

    for (const auto& name : list_dir(dir)) {
        // Iterative glob with backtracking on the single '*' runs we actually
        // use. Patterns here are fixed literals ("ob_*.zip", "config2.dat.*"),
        // so the 2^n worst case is never reached in practice.
        const auto lower = ascii_lower(name);
        std::size_t n = 0, m = 0, star = std::wstring::npos, mark = 0;
        while (n < lower.size()) {
            if (m < pat.size() && (pat[m] == L'?' || pat[m] == lower[n])) {
                ++n;
                ++m;
            } else if (m < pat.size() && pat[m] == L'*') {
                star = m++;
                mark = n;
            } else if (star != std::wstring::npos) {
                m = star + 1;
                n = ++mark;
            } else {
                break;
            }
        }
        if (n == lower.size() && m == pat.size()) out.push_back(name);
    }
    return out;
}

// ── Files ────────────────────────────────────────────────────────────────────

bool read_file(const std::wstring& path, std::string& out) {
    out.clear();

    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(h, &size)) {
        ::CloseHandle(h);
        return false;
    }
    if (size.QuadPart == 0) {
        ::CloseHandle(h);
        return true;
    }

    std::string buf(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ::ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    ::CloseHandle(h);
    if (!ok) return false;

    buf.resize(read); // short reads happen if GameLoop is mid-write
    out = std::move(buf);
    return true;
}

bool write_file(const std::wstring& path, std::string_view data) {
    // Writing over a Read-only smk.conf fails with ERROR_ACCESS_DENIED. The
    // quarantine fix locks that file on purpose, so every write must be able
    // to break the lock; callers re-apply the lock afterwards.
    ::SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);

    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool ok = true;
    std::size_t off = 0;
    while (off < data.size()) {
        const auto chunk = static_cast<DWORD>(
            (std::min)(data.size() - off, static_cast<std::size_t>(1u << 20)));
        DWORD written = 0;
        if (!::WriteFile(h, data.data() + off, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        off += written;
    }

    ::CloseHandle(h);
    return ok;
}

bool copy_file(const std::wstring& from, const std::wstring& to) {
    if (!::CopyFileW(from.c_str(), to.c_str(), FALSE)) return false;
    // CopyFileW already carries the attributes across, including ReadOnly, but
    // a ReadOnly destination would block the next restore — clear it so the
    // backup chain stays writable.
    ::SetFileAttributesW(to.c_str(), FILE_ATTRIBUTE_NORMAL);
    return true;
}

bool remove_file(const std::wstring& p) {
    ::SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
    return ::DeleteFileW(p.c_str()) != FALSE;
}

bool move_file(const std::wstring& from, const std::wstring& to) {
    // Clear a ReadOnly source first — os.replace() semantics in the Python build
    // would fail the same way otherwise.
    ::SetFileAttributesW(from.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (::MoveFileW(from.c_str(), to.c_str())) return true;

    if (!copy_file(from, to)) return false;
    return remove_file(from);
}

bool create_dirs(const std::wstring& p) {
    if (p.empty()) return false;
    if (dir_exists(p)) return true;

    const DWORD attr = ::GetFileAttributesW(p.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;

    const std::wstring parent = path_parent(p);
    if (!parent.empty() && parent != p && !dir_exists(parent)) {
        if (!create_dirs(parent)) return false;
    }
    return ::CreateDirectoryW(p.c_str(), nullptr) != FALSE || dir_exists(p);
}

bool set_readonly(const std::wstring& p, bool ro) {
    if (!file_exists(p)) return false;
    if (ro) {
        // OR the attribute in: the Archive bit is load-bearing for Explorer.
        return ::SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE;
    }
    return ::SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL) != FALSE;
}

bool is_readonly(const std::wstring& p) {
    const DWORD attr = ::GetFileAttributesW(p.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    return (attr & FILE_ATTRIBUTE_READONLY) != 0;
}

// ── Text ─────────────────────────────────────────────────────────────────────

std::string_view trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r\n\f\v");
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n\f\v");
    return s.substr(b, e - b + 1);
}

std::string_view rtrim(std::string_view s) {
    const auto e = s.find_last_not_of(" \t\r\n\f\v");
    if (e == std::string_view::npos) return {};
    return s.substr(0, e + 1);
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const auto pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out.append(sep);
        out.append(parts[i]);
    }
    return out;
}

std::vector<std::string> split_lines(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    const std::size_t n = s.size();

    while (i < n) {
        std::size_t j = i;
        while (j < n && s[j] != '\r' && s[j] != '\n') ++j;
        out.emplace_back(s.substr(i, j - i));

        if (j < n && s[j] == '\r') ++j;
        if (j < n && s[j] == '\n') ++j;
        i = j;
    }
    return out;
}

std::string replace_all(std::string_view s, std::string_view from, std::string_view to) {
    if (from.empty()) return std::string(s);
    std::string out;
    std::size_t pos = 0;
    while (true) {
        const auto hit = s.find(from, pos);
        if (hit == std::string_view::npos) {
            out.append(s.substr(pos));
            break;
        }
        out.append(s.substr(pos, hit - pos));
        out.append(to);
        pos = hit + from.size();
    }
    return out;
}

bool version_less(std::wstring_view a, std::wstring_view b) {
    // Compare numerically per dot-separated component, falling back to a
    // case-insensitive lexical compare for non-numeric parts.
    //
    // This exists to fix a latent bug in the Python build: app_version_dir()
    // used Python's sorted() on "7.0.167.0" and "10.0.0.0", which is a lexical
    // sort, so "10.0.0.0" sorted FIRST and the [-1] picked the older build.
    // The bug only surfaced after a major-version bump, which is exactly when
    // the user least wants the wrong Application\ dir.
    std::size_t ia = 0, ib = 0;
    while (ia <= a.size() && ib <= b.size()) {
        // Pull one dot-separated component from each.
        const auto ea = (ia < a.size()) ? a.find(L'.', ia) : std::wstring_view::npos;
        const auto eb = (ib < b.size()) ? b.find(L'.', ib) : std::wstring_view::npos;
        const auto parta = a.substr(ia, ea == std::wstring_view::npos ? std::wstring_view::npos
                                                                      : ea - ia);
        const auto partb = b.substr(ib, eb == std::wstring_view::npos ? std::wstring_view::npos
                                                                      : eb - ib);

        if (parta.empty() && partb.empty()) break;

        const bool na = !parta.empty() && parta.find_first_not_of(L"0123456789") == std::wstring_view::npos;
        const bool nb = !partb.empty() && partb.find_first_not_of(L"0123456789") == std::wstring_view::npos;

        if (na && nb) {
            const auto va = std::stoull(std::wstring(parta));
            const auto vb = std::stoull(std::wstring(partb));
            if (va != vb) return va < vb;
        } else if (na != nb) {
            // Numeric components outrank alphabetic ones: "10" > "beta", so
            // "beta" sorts BEFORE "10". Returning na directly would invert
            // that and pick the wrong Application\<ver\> directory.
            return !na;
        } else {
            const auto la = ascii_lower(parta);
            const auto lb = ascii_lower(partb);
            if (la != lb) return la < lb;
        }

        if (ea == std::wstring_view::npos && eb == std::wstring_view::npos) break;
        ia = (ea == std::wstring_view::npos) ? a.size() + 1 : ea + 1;
        ib = (eb == std::wstring_view::npos) ? b.size() + 1 : eb + 1;
    }
    return a.size() < b.size();
}

// ── Formatting ───────────────────────────────────────────────────────────────

namespace {

// Shared SYSTEMTIME snapshot. GetLocalTime is cheap enough that the three
// public helpers can each call it; this just keeps the format code in one place.
std::string format_time(const wchar_t* fmt) {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    // %u wants unsigned int; the SYSTEMTIME fields are WORD (unsigned short).
    const unsigned y = st.wYear, mo = st.wMonth, d = st.wDay;
    const unsigned h = st.wHour, mi = st.wMinute, s = st.wSecond;

    wchar_t buf[64]{};
    const int n = ::swprintf_s(buf, fmt, y, mo, d, h, mi, s);
    if (n <= 0) return {};
    return to_utf8(std::wstring_view(buf, static_cast<std::size_t>(n)));
}

} // namespace

std::string timestamp_compact() { return format_time(L"%04u%02u%02u-%02u%02u%02u"); }

std::string timestamp_readable() { return format_time(L"%04u-%02u-%02u %02u:%02u:%02u"); }

std::string timestamp_time() { return format_time(L"%02u:%02u:%02u"); }

std::wstring timestamp_compact_w() { return to_wide(timestamp_compact()); }

// ── Scrub ────────────────────────────────────────────────────────────────────

std::string scrub_secrets(std::string_view text) {
    // Mirrors log.py's regex:
    //   ("?)(seed_id|...)\1\s*[:=]\s*"?[^",\s}\]]+"
    // Two accepted shapes:  "key": "value"   (JSON, quoted key)
    //                       key = value      (text / query-string style)
    std::string out;
    out.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];

        // Try to read a key token starting here.
        std::size_t key_start = i;
        bool quoted = false;
        if (c == '"') {
            quoted = true;
            ++key_start;
        }
        const auto key_end = text.find_first_of(quoted ? '"' : ':', key_start);
        if (key_end == std::string_view::npos || key_end == key_start) {
            out.push_back(c);
            ++i;
            continue;
        }

        const std::string_view key = text.substr(key_start, key_end - key_start);
        if (!is_secret_key(key)) {
            out.push_back(c);
            ++i;
            continue;
        }

        // Skip past the closing quote / colon, then optional whitespace.
        std::size_t j = key_end + 1;
        if (quoted) {
            // Require the JSON separator shape: "key"  :  "value"
            auto k = j;
            while (k < text.size() && (text[k] == ' ' || text[k] == '\t')) ++k;
            if (k >= text.size() || text[k] != ':') {
                out.push_back(c);
                ++i;
                continue;
            }
            j = k + 1;
        }
        while (j < text.size() && (text[j] == ' ' || text[j] == '\t' || text[j] == '=')) ++j;

        // Preserve the key itself, replace the value.
        out.append(text.substr(i, j - i));
        if (j < text.size() && text[j] == '"') {
            out.push_back('"');
            ++j;
            const auto val_end = text.find_first_of("\",\r\n\t }]", j);
            out.append("***");
            if (val_end != std::string_view::npos) {
                out.push_back(text[val_end]);
                j = val_end + 1;
            } else {
                j = text.size();
            }
        } else {
            const auto val_end = text.find_first_of(",\r\n\t }]", j);
            out.append("***");
            j = (val_end == std::string_view::npos) ? text.size() : val_end;
        }
        i = j;
    }

    return out;
}

} // namespace glt::str