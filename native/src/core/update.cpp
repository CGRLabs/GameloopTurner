// glt/core/update.cpp — see glt/core/update.hpp for the design/security notes.
#include "glt/core/update.hpp"

#include "glt/core/strings.hpp"

#include <nlohmann/json.hpp>

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctype.h>
#include <vector>

#ifndef GLT_VERSION
#define GLT_VERSION "2.0.0"
#endif

namespace glt::update {
namespace {

// The app version, from the build (single source of truth: project VERSION).
std::string report_version() { return GLT_VERSION; }

// The portable exe name that matches THIS build's variant. The GLT_TARGET_*
// macros are public defines on the variant core, so this resolves per binary.
const char* compiled_variant_exe() {
#if defined(GLT_TARGET_HUB)
    return kHubExe;
#elif defined(GLT_TARGET_BETA)
    return kBetaExe;
#else
#error "Build must define exactly one of GLT_TARGET_BETA / GLT_TARGET_HUB (see native/CMakeLists.txt)"
#endif
}

// ── WinHTTP session helper ───────────────────────────────────────────────────
//
// Everything in this file uses one shape: open session → connect host → GET
// path → read body. Wrapped so fetch_latest() and download_verified() cannot
// drift apart in timeout or header policy.

struct Session {
    HINTERNET h = nullptr;
    Session() {
        // DEFAULT_PROXY honours the user's (or a corporate) proxy config; the
        // update must not hardcode "direct". The UA is required by the GitHub
        // API and doubles as the identifier the maintainer sees in access logs.
        std::wstring ua = L"GameLoopTuner/" + str::to_wide(report_version());
        h = ::WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (h) {
            // Resolve/connect/send 5s, receive 30s: the check runs on a worker
            // and a hung Wi-Fi captive portal must never stall it for minutes.
            ::WinHttpSetTimeouts(h, 5000, 5000, 5000, 30000);
        }
    }
    ~Session() {
        if (h) ::WinHttpCloseHandle(h);
    }
    explicit operator bool() const { return h != nullptr; }
};

struct Request {
    HINTERNET h = nullptr;
    ~Request() {
        if (h) ::WinHttpCloseHandle(h);
    }
    explicit operator bool() const { return h != nullptr; }
};

void throw_hresult(const char* what, DWORD err) {
    throw Error(std::string(what) + " (WinHTTP error " + std::to_string(err) + ")");
}

std::string read_body(HINTERNET req) {
    std::string body;
    for (;;) {
        DWORD avail = 0;
        if (!::WinHttpQueryDataAvailable(req, &avail)) {
            throw_hresult("query data availability", ::GetLastError());
        }
        if (avail == 0) break;
        std::size_t before = body.size();
        body.resize(before + avail);
        DWORD got = 0;
        if (!::WinHttpReadData(req, body.data() + before, avail, &got)) {
            throw_hresult("read response", ::GetLastError());
        }
        body.resize(before + got);
        if (got == 0) break;
    }
    return body;
}

void check_status(HINTERNET req, const char* what) {
    DWORD status = 0, size = sizeof(status);
    if (!::WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                               WINHTTP_NO_HEADER_INDEX)) {
        throw_hresult("query status", ::GetLastError());
    }
    if (status != 200) {
        throw Error(std::string(what) + ": HTTP " + std::to_string(status));
    }
}

// HTTPS GET with the GitHub API headers, body returned. `host`/`path` split so
// the download can target the release-asset CDN host the API hands back.
std::string https_get(const std::wstring& host, const std::wstring& path,
                      const char* what) {
    Session sess;
    if (!sess) throw_hresult("open WinHTTP session", ::GetLastError());

    Request conn;
    conn.h = ::WinHttpConnect(sess.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) throw_hresult("connect", ::GetLastError());

    Request req;
    req.h = ::WinHttpOpenRequest(conn.h, L"GET", path.c_str(), nullptr,
                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 WINHTTP_FLAG_SECURE);
    if (!req) throw_hresult("open request", ::GetLastError());

    const std::wstring headers =
        L"User-Agent: GameLoopTuner/" + str::to_wide(report_version()) +
        L"\r\nAccept: application/vnd.github+json"
        L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!::WinHttpAddRequestHeaders(req.h, headers.c_str(),
                                    static_cast<DWORD>(headers.size()),
                                    WINHTTP_ADDREQ_FLAG_ADD)) {
        throw_hresult("add request headers", ::GetLastError());
    }

    if (!::WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        throw_hresult("send request", ::GetLastError());
    }
    if (!::WinHttpReceiveResponse(req.h, nullptr)) {
        throw_hresult("receive response", ::GetLastError());
    }
    check_status(req.h, what);
    return read_body(req.h);
}

// ── SHA-256 via BCrypt ───────────────────────────────────────────────────────

std::string to_hex(const std::uint8_t* bytes, std::size_t n) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(kHex[bytes[i] >> 4]);
        out.push_back(kHex[bytes[i] & 0xF]);
    }
    return out;
}

// Reusable BCrypt SHA-256: hash arbitrary bytes incrementally, finish to hex.
struct Sha256 {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<std::uint8_t, 32> digest{};
    bool ok = false;

    Sha256() {
        NTSTATUS st = ::BCryptOpenAlgorithmProvider(&alg, L"SHA256", nullptr, 0);
        if (st < 0) return;
        st = ::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
        ok = st >= 0;
    }
    ~Sha256() {
        if (hash) ::BCryptDestroyHash(hash);
        if (alg) ::BCryptCloseAlgorithmProvider(alg, 0);
    }
    bool update(const void* data, std::size_t n) {
        if (!ok) return false;
        // HashData takes a ULONG; stream in 64 KiB chunks so file size never
        // changes the call shape.
        const auto* p = static_cast<const std::uint8_t*>(data);
        while (n > 0) {
            const ULONG chunk = static_cast<ULONG>(std::min<std::size_t>(n, 65536));
            if (::BCryptHashData(hash, const_cast<PUCHAR>(p), chunk, 0) < 0) return false;
            p += chunk;
            n -= chunk;
        }
        return true;
    }
    std::string finish_hex() {
        if (!ok) return {};
        if (::BCryptFinishHash(hash, digest.data(),
                               static_cast<ULONG>(digest.size()), 0) < 0) {
            return {};
        }
        return to_hex(digest.data(), digest.size());
    }
};

} // namespace

// ── Versions ─────────────────────────────────────────────────────────────────

std::optional<SemVer> parse_version(std::string_view tag) {
    std::string_view v = tag;
    if (!v.empty() && (v.front() == 'v' || v.front() == 'V')) v.remove_prefix(1);
    if (v.empty()) return std::nullopt;

    SemVer out{};
    int* slots[] = {&out.major, &out.minor, &out.patch};
    std::size_t i = 0;
    int slot = 0;

    for (;;) {
        // One numeric run per component.
        std::size_t d = 0;
        while (i + d < v.size() && isdigit(static_cast<unsigned char>(v[i + d]))) {
            ++d;
        }
        if (d == 0 || d > 9) return std::nullopt;

        int value = 0;
        for (std::size_t k = 0; k < d; ++k) {
            value = value * 10 + (v[i + k] - '0');
        }
        *slots[slot] = value;
        ++slot;
        i += d;

        if (i >= v.size()) break; // clean end

        if (v[i] != '.') return std::nullopt; // junk like "2x"
        ++i;
        if (i >= v.size()) return std::nullopt; // trailing dot

        if (slot == 3) {
            // Everything after three numeric components is a prerelease label
            // ("1.2.0.beta" — the published tags are not always pure semver).
            // Tolerated and ignored for comparison, but ONLY when it does not
            // look like a fourth version component: "2.0.0.1" stays refused.
            if (isdigit(static_cast<unsigned char>(v[i]))) return std::nullopt;
            break;
        }
    }
    return out; // missing minor/patch count as 0
}

bool is_newer(std::string_view tag, std::string_view current) {
    const auto a = parse_version(tag);
    const auto b = parse_version(current);
    if (!a || !b) return false;
    if (a->major != b->major) return a->major > b->major;
    if (a->minor != b->minor) return a->minor > b->minor;
    return a->patch > b->patch;
}

// ── Release selection ────────────────────────────────────────────────────────

std::optional<Release> parse_latest_json(std::string_view body,
                                         std::string_view variant_exe) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(body);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (!j.is_object() || !j.contains("tag_name") || !j["tag_name"].is_string()) {
        return std::nullopt;
    }

    Release r;
    r.tag = j["tag_name"].get<std::string>();
    if (j.contains("html_url") && j["html_url"].is_string()) {
        r.page_url = j["html_url"].get<std::string>();
    }
    if (j.contains("body") && j["body"].is_string()) {
        r.notes = j["body"].get<std::string>();
    }
    if (!j.contains("assets") || !j["assets"].is_array()) return std::nullopt;

    // SECURITY: no digest, no install. The digest field ("sha256:<hex>") comes
    // from the release API over the same TLS channel as everything else — it
    // pins the download to what the maintainer published, so a later CDN
    // substitution is caught by the hash. A candidate without one is skipped,
    // never trusted.
    std::optional<Release> portable_candidate;
    for (const auto& a : j["assets"]) {
        if (!a.is_object() || !a.contains("name") || !a["name"].is_string()) continue;
        const std::string name = a["name"].get<std::string>();

        const bool is_setup = str::iequals(name, kSetupAsset);
        const bool is_variant = !variant_exe.empty() && str::iequals(name, variant_exe);
        if (!is_setup && !is_variant) continue;

        std::string digest;
        if (a.contains("digest") && a["digest"].is_string()) {
            digest = a["digest"].get<std::string>();
        }
        constexpr std::string_view kPrefix = "sha256:";
        if (digest.size() <= kPrefix.size() ||
            digest.compare(0, kPrefix.size(), kPrefix) != 0) {
            continue; // unusable candidate — keep scanning
        }
        if (!a.contains("browser_download_url") || !a["browser_download_url"].is_string()) {
            continue;
        }

        Release cand;
        cand.tag = r.tag;
        cand.page_url = r.page_url;
        cand.notes = r.notes;
        cand.kind = is_setup ? Kind::Installer : Kind::Portable;
        cand.asset_name = name;
        cand.asset_url = a["browser_download_url"].get<std::string>();
        cand.sha256 = str::ascii_lower(digest.substr(kPrefix.size()));
        if (a.contains("size") && a["size"].is_number_unsigned()) {
            cand.asset_size = a["size"].get<std::uint64_t>();
        }

        if (is_setup) {
            return cand; // preferred the moment it appears
        }
        if (!portable_candidate.has_value()) {
            portable_candidate = std::move(cand);
        }
    }

    if (portable_candidate.has_value()) {
        return *portable_candidate;
    }
    return r; // valid release, Kind::None — the UI sends the user to the page
}

Release fetch_latest() {
    const std::string body =
        https_get(kRepoApiHost, str::to_wide(kRepoApiLatest), "update check");
    auto r = parse_latest_json(body, compiled_variant_exe());
    if (!r) {
        throw Error("could not parse the latest-release response");
    }
    return std::move(*r);
}

// ── Download + verify ────────────────────────────────────────────────────────

std::string sha256_file(const std::wstring& path) {
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};

    Sha256 s;
    std::string hex;
    std::vector<char> buf(65536);
    for (;;) {
        DWORD got = 0;
        if (!::ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
            break;
        }
        if (got == 0) {
            hex = s.finish_hex();
            break;
        }
        if (!s.update(buf.data(), got)) break;
    }
    ::CloseHandle(f);
    return hex;
}

std::string download_verified(
    const Release& r, const std::wstring& dest,
    const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
    if (r.sha256.empty()) {
        throw Error("release asset has no sha256 digest; refusing to download");
    }

    // browser_download_url: https://<host>/<path...>
    const std::string url = r.asset_url;
    constexpr std::string_view kScheme = "https://";
    if (url.compare(0, kScheme.size(), kScheme) != 0) {
        throw Error("release asset URL is not HTTPS");
    }
    const std::size_t slash = url.find('/', kScheme.size());
    if (slash == std::string::npos) {
        throw Error("release asset URL has no path");
    }
    const std::wstring host = str::to_wide(url.substr(kScheme.size(), slash - kScheme.size()));
    const std::wstring path = str::to_wide(url.substr(slash));

    Session sess;
    if (!sess) throw_hresult("open WinHTTP session", ::GetLastError());
    Request conn;
    conn.h = ::WinHttpConnect(sess.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn) throw_hresult("connect", ::GetLastError());
    Request req;
    req.h = ::WinHttpOpenRequest(conn.h, L"GET", path.c_str(), nullptr,
                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 WINHTTP_FLAG_SECURE);
    if (!req) throw_hresult("open request", ::GetLastError());

    const std::wstring headers =
        L"User-Agent: GameLoopTuner/" + str::to_wide(report_version()) + L"\r\n";
    if (!::WinHttpAddRequestHeaders(req.h, headers.c_str(),
                                    static_cast<DWORD>(headers.size()),
                                    WINHTTP_ADDREQ_FLAG_ADD)) {
        throw_hresult("add request headers", ::GetLastError());
    }
    if (!::WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        throw_hresult("send request", ::GetLastError());
    }
    if (!::WinHttpReceiveResponse(req.h, nullptr)) {
        throw_hresult("receive response", ::GetLastError());
    }
    check_status(req.h, "asset download");

    std::uint64_t total = 0;
    {
        DWORD size = sizeof(total);
        DWORD idx = 0;
        if (::WinHttpQueryHeaders(req.h,
                                  WINHTTP_QUERY_CONTENT_LENGTH |
                                      WINHTTP_QUERY_FLAG_NUMBER64,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &total, &size,
                                  &idx)) {
            // total valid; some CDNs omit it and progress just runs unsized.
        } else {
            total = 0;
        }
    }

    HANDLE f = ::CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        throw Error("could not create " + str::to_utf8(dest) +
                    " (error " + std::to_string(::GetLastError()) + ")");
    }

    Sha256 hash;
    std::uint64_t done = 0;
    auto fail = [&](const std::string& msg) {
        ::CloseHandle(f);
        ::DeleteFileW(dest.c_str()); // never leave a half-written exe behind
        throw Error(msg);
    };

    for (;;) {
        DWORD avail = 0;
        if (!::WinHttpQueryDataAvailable(req.h, &avail)) {
            fail("query download availability (WinHTTP error " +
                 std::to_string(::GetLastError()) + ")");
        }
        if (avail == 0) break;

        std::vector<char> chunk(avail);
        DWORD got = 0;
        if (!::WinHttpReadData(req.h, chunk.data(), avail, &got)) {
            fail("read download (WinHTTP error " + std::to_string(::GetLastError()) + ")");
        }
        if (got == 0) break;

        if (!hash.update(chunk.data(), got)) fail("hashing failed");
        DWORD written = 0;
        if (!::WriteFile(f, chunk.data(), got, &written, nullptr) || written != got) {
            fail("write to disk failed (error " + std::to_string(::GetLastError()) + ")");
        }
        done += got;
        if (progress) progress(done, total);
    }
    ::CloseHandle(f);

    const std::string actual = hash.finish_hex();
    if (actual != r.sha256) {
        ::DeleteFileW(dest.c_str());
        throw Error("SHA-256 mismatch: expected " + r.sha256 + ", got " + actual +
                    ". The download did not match the release manifest and was deleted.");
    }

    // Belt and braces: re-hash from disk so the file the user will run is the
    // file that was verified, not the bytes still in flight.
    const std::string on_disk = sha256_file(dest);
    if (on_disk != r.sha256) {
        ::DeleteFileW(dest.c_str());
        throw Error("SHA-256 of the file on disk does not match; download deleted.");
    }
    return on_disk;
}

// ── Install ──────────────────────────────────────────────────────────────────

bool launch_installer(const std::wstring& setup_path, std::string& error) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas"; // the installer manifest is asInvoker; this raises it
    sei.lpFile = setup_path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_DEFAULT;
    if (!::ShellExecuteExW(&sei)) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_CANCELLED) {
            error = "update cancelled (the UAC prompt was declined)";
        } else {
            error = "could not launch the installer (error " + std::to_string(err) + ")";
        }
        return false;
    }
    return true;
}

bool self_replace(const std::wstring& new_exe_path, std::string& error) {
    wchar_t running[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(nullptr, running, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        error = "could not resolve the running executable path";
        return false;
    }
    const std::wstring running_path(running, n);
    const std::wstring old_path = running_path + L".old";

    // 1. Park the running image. Renaming a running exe is legal on Windows —
    //    the image section stays mapped — which is what makes a portable
    //    self-update possible without any helper process.
    if (!::MoveFileExW(running_path.c_str(), old_path.c_str(),
                       MOVEFILE_REPLACE_EXISTING)) {
        error = "could not move the current binary aside (error " +
                std::to_string(::GetLastError()) + ")";
        return false;
    }

    // 2. Move the verified download into the running binary's exact name and
    //    directory, so a renamed or relocated copy stays renamed/relocated.
    if (!::MoveFileExW(new_exe_path.c_str(), running_path.c_str(),
                       MOVEFILE_REPLACE_EXISTING)) {
        // Roll the park back so the app remains exactly as it was.
        ::MoveFileExW(old_path.c_str(), running_path.c_str(),
                      MOVEFILE_REPLACE_EXISTING);
        error = "could not move the new binary into place (error " +
                std::to_string(::GetLastError()) + ")";
        return false;
    }

    // 3. Relaunch. The new binary's embedded manifest re-raises UAC; a declined
    //    prompt is not fatal — the new version is already in place and starts
    //    on the user's next manual launch.
    const HINSTANCE h = ::ShellExecuteW(nullptr, L"open", running_path.c_str(),
                                        nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(h) <= 32) {
        error = "the updated tuner is in place, but relaunching failed (error " +
                std::to_string(::GetLastError()) + ")";
        return false;
    }
    return true;
}

void cleanup_previous_update() {
    wchar_t running[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(nullptr, running, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring old_path(running, n);
    old_path += L".old";
    ::DeleteFileW(old_path.c_str()); // best-effort: gone, or locked, either is fine
}

std::wstring running_exe_dir() {
    wchar_t running[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(nullptr, running, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return str::path_parent(std::wstring(running, n));
}

} // namespace glt::update
