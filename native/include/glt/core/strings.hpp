// ─────────────────────────────────────────────────────────────────────────────
// glt/core/strings.hpp — encoding, path and text primitives.
//
// CONTRACT: std::string is the canonical in-memory representation and always
// holds UTF-8 bytes. std::wstring appears only at Win32 API boundaries.
//
// This matters for two reverse-engineered files we round-trip byte-for-byte:
//   * smk.conf  — UTF-8, no BOM, LF-only line endings, CJK in comm="..."
//   * hosts     — must not gain CRLF translations
// Reading or writing either through a text-mode handle on Windows silently
// injects \r\n and breaks the next regex pass over smk.conf. Every file helper
// here therefore opens in binary mode.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace glt::str {

// ── Encoding ─────────────────────────────────────────────────────────────────

// UTF-8 → UTF-16. Invalid bytes become U+FFFD rather than throwing, so a
// corrupt smk.conf can never take the process down mid-apply.
[[nodiscard]] std::wstring to_wide(std::string_view utf8);

// UTF-16 → UTF-8. Unpaired surrogates are replaced.
[[nodiscard]] std::string to_utf8(std::wstring_view utf16);

[[nodiscard]] bool is_valid_utf8(std::string_view s);

// ── Case ─────────────────────────────────────────────────────────────────────

// ASCII-only lower/upper. Deliberately not locale-aware: every case-insensitive
// comparison in this app is against ASCII identifiers (hostnames, .exe names,
// registry value names), and locale-aware casing would make those results
// depend on the user's regional settings.
[[nodiscard]] std::string ascii_lower(std::string_view s);
[[nodiscard]] std::wstring ascii_lower(std::wstring_view s);

[[nodiscard]] bool iequals(std::string_view a, std::string_view b);
[[nodiscard]] bool iequals(std::wstring_view a, std::wstring_view b);

[[nodiscard]] bool icontains(std::string_view haystack, std::string_view needle);

// ── Paths ────────────────────────────────────────────────────────────────────

// Join with a single '\'. Absolute `tail` replaces `head`.
[[nodiscard]] std::wstring path_join(std::wstring_view head, std::wstring_view tail);

[[nodiscard]] std::wstring path_parent(std::wstring_view p);
[[nodiscard]] std::wstring path_filename(std::wstring_view p);
[[nodiscard]] std::wstring path_stem(std::wstring_view p);      // no extension
[[nodiscard]] std::wstring path_extension(std::wstring_view p); // ".exe" or ""

// "C:\Tencent\GameLoop" → "C:"  (empty when the path has no drive letter)
[[nodiscard]] std::wstring path_root(std::wstring_view p);

[[nodiscard]] bool file_exists(std::wstring_view p);
[[nodiscard]] bool dir_exists(std::wstring_view p);
[[nodiscard]] std::uint64_t file_size(std::wstring_view p);

// Free space in bytes on the volume holding `p`. Returns false on failure.
[[nodiscard]] bool disk_free_bytes(std::wstring_view p, std::uint64_t& out);

// Directory entries (names only, no "." / ".."). Returns an empty vector when
// the directory cannot be opened.
[[nodiscard]] std::vector<std::wstring> list_dir(std::wstring_view dir);

// fnmatch-style glob over list_dir(). '*' matches any run, '?' one char.
// Case-insensitive, because Windows filesystems are.
[[nodiscard]] std::vector<std::wstring> glob(std::wstring_view dir, std::wstring_view pattern);

// ── Files ────────────────────────────────────────────────────────────────────

// Binary read. Returns false if the file is missing or unreadable.
// Does NOT strip a BOM — callers that care must handle it themselves, because
// a BOM is meaningful when comparing against GameLoop's own output.
[[nodiscard]] bool read_file(const std::wstring& path, std::string& out);

// Binary write. Creates/truncates. Keeps the destination's ReadOnly attribute
// off by clearing it first (quarantine locks smk.conf deliberately).
[[nodiscard]] bool write_file(const std::wstring& path, std::string_view data);

// Copy preserving mtime. Falls back to a plain read/write when attributes
// cannot be cloned.
[[nodiscard]] bool copy_file(const std::wstring& from, const std::wstring& to);

[[nodiscard]] bool remove_file(const std::wstring& p);

// Move, falling back to copy+delete across volumes.
[[nodiscard]] bool move_file(const std::wstring& from, const std::wstring& to);

[[nodiscard]] bool create_dirs(const std::wstring& p);

// Sets/clears FILE_ATTRIBUTE_READONLY. Returns false if the file is missing.
// This is the durable kill-switch for smk.conf — see docs/keymap-auto-switch.md.
[[nodiscard]] bool set_readonly(const std::wstring& p, bool ro);
[[nodiscard]] bool is_readonly(const std::wstring& p);

// ── Text ─────────────────────────────────────────────────────────────────────

[[nodiscard]] std::string_view trim(std::string_view s);
[[nodiscard]] std::string_view rtrim(std::string_view s);

[[nodiscard]] std::vector<std::string> split(std::string_view s, char sep);
[[nodiscard]] std::string join(const std::vector<std::string>& parts, std::string_view sep);

// Splits on \r\n, \n or \r. A trailing newline does not produce a final empty
// element, matching Python's str.splitlines().
[[nodiscard]] std::vector<std::string> split_lines(std::string_view s);

[[nodiscard]] std::string replace_all(std::string_view s, std::string_view from, std::string_view to);

// Non-zero-based left/right trim of digits — used for the version-dir sort.
[[nodiscard]] bool version_less(std::wstring_view a, std::wstring_view b);

// ── Formatting ───────────────────────────────────────────────────────────────

// "20261015-142233" — backup filename suffix, seconds resolution.
[[nodiscard]] std::string timestamp_compact();

// "2026-10-15 14:22:33"
[[nodiscard]] std::string timestamp_readable();

// "14:22:33" — log console prefix.
[[nodiscard]] std::string timestamp_time();

// "20261015-142233" as UTF-16.
[[nodiscard]] std::wstring timestamp_compact_w();

// ── Logging helper: scrub secrets ───────────────────────────────────────────

// Redacts the key names GameLoop uses for per-device identifiers, so nothing
// identifying ever reaches the log file or a diagnostic report. The pattern is
// the same one log.py used:  ("?)(key)\1\s*[:=]\s*"?value"?
[[nodiscard]] std::string scrub_secrets(std::string_view text);

} // namespace glt::str