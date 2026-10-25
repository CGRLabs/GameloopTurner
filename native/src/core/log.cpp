#include "glt/core/log.hpp"

#include "glt/core/strings.hpp"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace glt::log {
namespace {

constexpr std::size_t kMaxMemLines = 500;
constexpr long kMaxFileBytes = 1'000'000;
constexpr int kFileBackups = 3;

std::mutex g_mutex;
std::vector<std::string> g_mem;
bool g_file_ready = false;
std::wstring g_file;

std::wstring resolve_log_dir() {
    // %TEMP% first — it is where the Python build put the log and where users
    // have been told to look when something goes wrong.
    DWORD n = ::GetEnvironmentVariableW(L"TEMP", nullptr, 0);
    if (n > 0) {
        std::wstring buf(n, L'\0');
        const DWORD got = ::GetEnvironmentVariableW(L"TEMP", buf.data(), n);
        buf.resize(got);
        if (!buf.empty()) return str::path_join(buf, L"gameloop-tuner");
    }

    // %TEMP% is unset in some service contexts; fall back to %LOCALAPPDATA%\Temp.
    PWSTR known = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known))) {
        std::wstring out = str::path_join(known, L"Temp\\gameloop-tuner");
        ::CoTaskMemFree(known);
        return out;
    }
    return {};
}

// Rotates to tuner.log.1 / .2 / .3 when the active file crosses kMaxFileBytes.
// Silently gives up if the directory cannot be created — the in-memory ring
// still works, which is what the UI actually depends on.
void ensure_file() {
    if (g_file_ready) return;
    g_file_ready = true;

    const std::wstring dir = resolve_log_dir();
    if (dir.empty() || !str::create_dirs(dir)) return;
    g_file = str::path_join(dir, L"tuner.log");
}

void rotate_if_needed() {
    if (g_file.empty()) return;
    if (str::file_size(g_file) < kMaxFileBytes) return;

    // Shift .2 → .3, .1 → .2, log → .1.
    const auto oldest = g_file + L"." + std::to_wstring(kFileBackups);
    str::remove_file(oldest);
    for (int i = kFileBackups - 1; i >= 1; --i) {
        const auto from = g_file + L"." + std::to_wstring(i);
        const auto to = g_file + L"." + std::to_wstring(i + 1);
        if (str::file_exists(from)) str::move_file(from, to);
    }
    str::move_file(g_file, g_file + L".1");
}

void write_file_line(std::string_view line) {
    ensure_file();
    if (g_file.empty()) return;
    rotate_if_needed();

    // Append through the Win32 API rather than std::ofstream: ofstream holds a
    // share mode that fights the rotate-rename above.
    HANDLE h = ::CreateFileW(g_file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    std::string buf(line);
    buf += "\r\n";
    DWORD written = 0;
    ::WriteFile(h, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr);
    ::CloseHandle(h);
}

} // namespace

std::string_view level_tag(Level l) {
    switch (l) {
        case Level::Info:     return "INFO";
        case Level::Success:  return "OK";
        case Level::Warning:  return "WARN";
        case Level::Error:    return "ERROR";
        case Level::Progress: return "RUN";
        case Level::Drift:    return "DRIFT";
    }
    return "INFO";
}

void write(Level level, std::string_view message) {
    // Scrub BEFORE formatting into the ring: the ring is what the report dumps.
    const std::string safe = str::scrub_secrets(message);
    const std::string line =
        str::timestamp_readable() + " " + std::string(level_tag(level)) + " " + safe;

    std::scoped_lock lock(g_mutex);

    g_mem.push_back(line);
    if (g_mem.size() > kMaxMemLines) {
        g_mem.erase(g_mem.begin(), g_mem.begin() + (g_mem.size() - kMaxMemLines));
    }

    write_file_line(line);
}

void info(std::string_view msg) { write(Level::Info, msg); }
void warn(std::string_view msg) { write(Level::Warning, msg); }
void error(std::string_view msg) { write(Level::Error, msg); }

std::vector<std::string> mem_lines() {
    std::scoped_lock lock(g_mutex);
    return g_mem;
}

void clear_mem() {
    std::scoped_lock lock(g_mutex);
    g_mem.clear();
}

std::wstring file_path() {
    std::scoped_lock lock(g_mutex);
    ensure_file();
    return g_file;
}

} // namespace glt::log