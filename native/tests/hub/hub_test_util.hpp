// ─────────────────────────────────────────────────────────────────────────────
// native/tests/hub/hub_test_util.hpp — helpers for the Hub suite.
//
// Only what the Hub tests need: a private temp file/database path so the tests
// never touch the real %APPDATA% or a live Configs.db.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <windows.h>

#include <string>

#include "glt/core/strings.hpp"

namespace hubtest {

inline std::wstring temp_dir() {
    wchar_t buf[MAX_PATH + 2]{};
    const DWORD n = ::GetTempPathW(MAX_PATH, buf);
    if (n == 0) return L".";
    std::wstring d(buf, n);
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    return d;
}

// A unique path in the temp directory. Not created on disk.
inline std::wstring unique_temp_path(const wchar_t* stem, const wchar_t* ext) {
    const std::wstring name = std::wstring(stem) + L"-" + glt::str::timestamp_compact_w() +
                              L"-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
                              std::to_wstring(::GetTickCount64()) + ext;
    return glt::str::path_join(temp_dir(), name);
}

// Removes a temp database plus any SQLite sidecars it may have left.
inline void remove_db(const std::wstring& path) {
    glt::str::remove_file(path);
    glt::str::remove_file(path + L"-wal");
    glt::str::remove_file(path + L"-shm");
    glt::str::remove_file(path + L"-journal");
}

} // namespace hubtest