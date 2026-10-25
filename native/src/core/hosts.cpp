#include "glt/core/hosts.hpp"

#include "glt/core/log.hpp"
#include "glt/core/strings.hpp"

#include <windows.h>
#include <ws2tcpip.h>

#include <algorithm>

namespace glt::hosts {
namespace {

// Winsock needs a one-time process-wide startup. probe() is the only consumer,
// so the guard is scoped to it rather than to main().
class WinsockGuard {
public:
    WinsockGuard() {
        WSADATA wsa{};
        ok_ = ::WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }
    ~WinsockGuard() {
        if (ok_) ::WSACleanup();
    }
    WinsockGuard(const WinsockGuard&) = delete;
    WinsockGuard& operator=(const WinsockGuard&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

// Line endings are detected from the file and preserved exactly. Rewriting a
// hosts file that was LF-terminated as CRLF-terminated is gratuitous churn in
// a file that ships with Windows and gets diffed by security tooling.
std::string_view detect_eol(const std::string& text) {
    const auto crlf = text.find("\r\n");
    const auto lf = text.find('\n');
    if (crlf != std::string::npos && (lf == std::string::npos || crlf <= lf)) {
        return "\r\n";
    }
    if (lf != std::string::npos) return "\n";
    return "\r\n";
}

std::vector<std::string> read_lines() {
    std::string text;
    if (!str::read_file(hosts_path(), text)) return {};

    auto lines = str::split_lines(text);
    // split_lines drops a trailing empty element only when the file ends with
    // a newline; a file with trailing blank lines keeps them, which is correct
    // for round-tripping.
    return lines;
}

std::wstring backup_hosts() {
    const std::wstring src = hosts_path();
    if (!str::file_exists(src)) return {};

    const std::wstring bak = src + L".bak-" + str::timestamp_compact_w();
    std::string bytes;
    if (!str::read_file(src, bytes) || !str::write_file(bak, bytes)) return {};

    return bak;
}

} // namespace

std::wstring hosts_path() {
    // GetWindowsDirectoryW, not %SystemRoot%: it is the API that accounts for
    // the WOW64 file-system redirection on a 32-bit process. We build x64, but
    // using the API keeps the module correct if that ever changes.
    wchar_t sys[MAX_PATH]{};
    const UINT n = ::GetWindowsDirectoryW(sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};

    return str::path_join(sys, L"System32\\drivers\\etc\\hosts");
}

bool is_blocked() {
    for (const auto& ln : read_lines()) {
        if (ln.find(kDomain) != std::string::npos &&
            ln.find("0.0.0.0") != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> block(bool dry_run) {
    std::vector<std::string> notes;

    if (is_blocked()) {
        notes.push_back("hosts: " + std::string(kDomain) + " already blocked");
        return notes;
    }

    const std::wstring path = hosts_path();
    if (!str::file_exists(path)) {
        notes.push_back("hosts: file not found");
        return notes;
    }

    if (dry_run) {
        notes.push_back("DRYRUN: would append \"0.0.0.0 " + std::string(kDomain) +
                        "\" to " + str::to_utf8(path));
        return notes;
    }

    std::string text;
    if (!str::read_file(path, text)) {
        notes.push_back("hosts: read failed");
        return notes;
    }

    const auto bak = backup_hosts();
    if (!bak.empty()) notes.push_back("hosts backup: " + str::to_utf8(bak));

    const std::string_view eol = detect_eol(text);
    std::string out = text;
    if (!out.empty() && out.back() != '\n') out += eol;
    out += eol; // the blank separator line
    out += "0.0.0.0 ";
    out += kDomain;
    out += " ";
    out += kMark;
    out += eol;

    if (!str::write_file(path, out)) {
        notes.push_back("hosts: write failed");
        return notes;
    }

    notes.push_back("hosts: blocked " + std::string(kDomain) + " (0.0.0.0)");
    log::info(notes.back());
    return notes;
}

std::vector<std::string> unblock(bool dry_run) {
    std::vector<std::string> notes;

    const std::wstring path = hosts_path();

    // Read the ORIGINAL text so the EOL style can be recovered from it, then
    // filter line-by-line. Rewriting through a joined string would lose the
    // distinction between LF and CRLF files.
    std::string text;
    if (!str::read_file(path, text)) {
        notes.push_back("hosts: file not found or unreadable");
        return notes;
    }

    auto lines = str::split_lines(text);

    std::vector<std::string> keep;
    for (auto& ln : lines) {
        if (ln.find(kDomain) != std::string::npos ||
            ln.find(kMark) != std::string::npos) {
            continue;
        }
        keep.push_back(std::move(ln));
    }

    if (keep.size() == lines.size()) {
        notes.push_back("hosts: no tuner block found");
        return notes;
    }

    if (dry_run) {
        notes.push_back("DRYRUN: would remove " + std::to_string(lines.size() - keep.size()) +
                        " hosts line(s)");
        return notes;
    }

    const auto bak = backup_hosts();
    if (!bak.empty()) notes.push_back("hosts backup: " + str::to_utf8(bak));

    const std::string_view eol = detect_eol(text);
    std::string out;
    for (const auto& ln : keep) {
        out += ln;
        out += eol;
    }

    if (!str::write_file(path, out)) {
        notes.push_back("hosts: write failed");
        return notes;
    }

    notes.push_back("hosts: unblocked " + std::string(kDomain));
    log::info(notes.back());
    return notes;
}

Probe probe() {
    Probe out;
    out.domain = std::string(kDomain);
    out.blocked = is_blocked();

    WinsockGuard wsa;
    if (!wsa.ok()) {
        out.error = "WSAStartup failed";
        out.tcp443 = "fail: " + out.error;
        return out;
    }

    // ── Resolve ────────────────────────────────────────────────────────────
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    const int rc = ::getaddrinfo(kDomain.data(), "443", &hints, &res);
    if (rc != 0) {
        out.error = "getaddrinfo: " + std::string(::gai_strerrorA(rc));
    } else {
        for (addrinfo* p = res; p; p = p->ai_next) {
            char buf[INET6_ADDRSTRLEN]{};
            if (!p->ai_addr) continue;

            if (p->ai_family == AF_INET) {
                auto* a = reinterpret_cast<sockaddr_in*>(p->ai_addr);
                ::inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
                out.resolved.emplace_back(buf);
            } else if (p->ai_family == AF_INET6) {
                auto* a = reinterpret_cast<sockaddr_in6*>(p->ai_addr);
                ::inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
                out.resolved.emplace_back(buf);
            }
        }
        ::freeaddrinfo(res);

        std::sort(out.resolved.begin(), out.resolved.end());
        out.resolved.erase(std::unique(out.resolved.begin(), out.resolved.end()),
                           out.resolved.end());
    }

    // ── TCP connect ────────────────────────────────────────────────────────
    // 4 s, matching socket.create_connection(timeout=4) in hosts.py.
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        out.tcp443 = "fail: socket() failed";
        return out;
    }

    DWORD timeout = 4000;
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));
    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
                 sizeof(timeout));

    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = ::htons(443);
    if (out.resolved.empty()) {
        out.tcp443 = "fail: no A record";
        ::closesocket(s);
        return out;
    }
    ::inet_pton(AF_INET, out.resolved.front().c_str(), &sa.sin_addr);

    if (::connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == 0) {
        out.tcp443 = "open";
    } else {
        out.tcp443 = "fail: WSA error " + std::to_string(::WSAGetLastError());
    }
    ::closesocket(s);

    return out;
}

} // namespace glt::hosts