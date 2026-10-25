#include "glt/core/netinfo.hpp"

#include "glt/core/paths.hpp"
#include "glt/core/strings.hpp"

#include <windows.h>
#include <shellapi.h>
#include <iphlpapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace glt::core::netinfo {
namespace {

// Run a command and capture its stdout. Returns empty on failure.
std::string run_command(const std::wstring& exe, const std::wstring& args) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outRead = nullptr, outWrite = nullptr;
    if (!::CreatePipe(&outRead, &outWrite, &sa, 0)) return {};
    ::SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.hStdOutput = outWrite;
    si.hStdError = outWrite;
    si.dwFlags = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    BOOL ok = ::CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    ::CloseHandle(outWrite);
    if (!ok) {
        ::CloseHandle(outRead);
        return {};
    }

    std::string out;
    char chunk[4096];
    DWORD got = 0;
    for (;;) {
        if (!::ReadFile(outRead, chunk, sizeof(chunk), &got, nullptr) || got == 0) break;
        out.append(chunk, got);
    }

    ::WaitForSingleObject(pi.hProcess, 15000);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(outRead);
    return out;
}

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// /proc/net stores IP as little-endian hex (8 hex digits).
std::string decode_ip(const std::string& hex) {
    if (hex.size() != 8) return hex;
    std::string out;
    for (int i = 6; i >= 0; i -= 2) {
        if (!out.empty()) out += ".";
        unsigned v = 0;
        std::sscanf(hex.c_str() + i, "%2x", &v);
        out += std::to_string(v);
    }
    return out;
}

int decode_port(const std::string& hex) {
    unsigned v = 0;
    std::sscanf(hex.c_str(), "%x", &v);
    return static_cast<int>(v);
}

std::string decode_state(const std::string& st, const std::string& proto) {
    if (proto == "UDP") {
        // UDP has no real state; 07 = unconnected/listening, 01 = connected.
        if (st == "07") return "LISTEN";
        if (st == "01") return "CONNECTED";
        return st;
    }
    static const std::vector<std::pair<std::string, std::string>> kStates = {
        {"01", "ESTABLISHED"}, {"02", "SYN_SENT"}, {"03", "SYN_RECV"},
        {"04", "FIN_WAIT1"}, {"05", "FIN_WAIT2"}, {"06", "TIME_WAIT"},
        {"07", "CLOSE"}, {"08", "CLOSE_WAIT"}, {"09", "LAST_ACK"},
        {"0A", "LISTEN"}, {"0B", "CLOSING"},
    };
    for (const auto& [k, v] : kStates) if (k == st) return v;
    return st;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

std::vector<Connection> parse_procnet(const std::string& text, const std::string& proto) {
    std::vector<Connection> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        auto toks = split_ws(line);
        if (toks.size() < 4) continue;
        if (toks[0] == "sl") continue; // header

        // toks[1] = local_address, toks[2] = rem_address, toks[3] = st
        auto local = toks[1];
        auto rem = toks[2];
        auto st = toks[3];

        auto lpos = local.find(':');
        auto rpos = rem.find(':');
        if (lpos == std::string::npos || rpos == std::string::npos) continue;

        Connection c;
        c.proto = proto;
        c.local_ip = decode_ip(local.substr(0, lpos));
        c.local_port = decode_port(local.substr(lpos + 1));
        c.remote_ip = decode_ip(rem.substr(0, rpos));
        c.remote_port = decode_port(rem.substr(rpos + 1));
        c.state = decode_state(st, proto);
        c.role = role_for(c.remote_ip, c.remote_port);

        // Skip pure loopback listeners — not interesting for users.
        if (c.remote_ip == "0.0.0.0" && c.state == "LISTEN") continue;
        if (c.remote_ip == "127.0.0.1" && c.remote_ip == c.local_ip) continue;

        out.push_back(std::move(c));
    }
    return out;
}

} // namespace

std::string role_for(const std::string& ip, int port) {
    if (ip.rfind("43.130.", 0) == 0 || ip.rfind("49.51.67.", 0) == 0) {
        if (port >= 5000) return "Game server";
        if (port == 443) return "Matchmaking / API";
        return "Tencent Cloud";
    }
    if (ip.rfind("172.217.", 0) == 0 || ip.rfind("142.250.", 0) == 0) {
        return "Telemetry (Google)";
    }
    if (ip == "172.17.224.1") return "DHCP gateway (host)";
    if (ip == "127.0.0.1" || ip == "0.0.0.0") return "Local";
    return "Unknown";
}

int ping_ms(const std::string& ip) {
    if (ip.empty() || ip == "0.0.0.0" || ip == "127.0.0.1") return -1;
    std::wstring wip(ip.begin(), ip.end());
    std::wstring args = L"-n 1 -w 1000 " + wip;
    std::string out = run_command(L"ping", args);
    // Parse "time=XXms" or "time<1ms".
    auto pos = out.find("time");
    if (pos == std::string::npos) return -1;
    auto eq = out.find('=', pos);
    if (eq == std::string::npos) return -1;
    std::string num;
    for (size_t i = eq + 1; i < out.size(); ++i) {
        char ch = out[i];
        if (std::isdigit(static_cast<unsigned char>(ch))) num += ch;
        else if (ch == '<') continue;
        else break;
    }
    if (num.empty()) return -1;
    try { return std::stoi(num); } catch (...) { return -1; }
}

std::string flush_dns() {
    return run_command(L"ipconfig", L"/flushdns");
}

void open_resource_monitor() {
    ::ShellExecuteW(nullptr, L"open", L"resmon.exe", nullptr, nullptr, SW_SHOWNORMAL);
}

std::vector<Connection> capture() {
    std::vector<Connection> out;
    std::wstring adb = paths::adb_exe();
    if (adb.empty()) return out;

    // Connect (idempotent).
    run_command(adb, L"-s 127.0.0.1:5555 connect 127.0.0.1:5555");

    std::string tcp = run_command(adb, L"-s 127.0.0.1:5555 shell cat /proc/net/tcp");
    std::string udp = run_command(adb, L"-s 127.0.0.1:5555 shell cat /proc/net/udp");

    auto tcpConns = parse_procnet(tcp, "TCP");
    auto udpConns = parse_procnet(udp, "UDP");
    out.insert(out.end(), tcpConns.begin(), tcpConns.end());
    out.insert(out.end(), udpConns.begin(), udpConns.end());

    // De-duplicate identical rows.
    std::vector<Connection> uniq;
    for (const auto& c : out) {
        bool dup = false;
        for (const auto& u : uniq) {
            if (u.proto == c.proto && u.remote_ip == c.remote_ip &&
                u.remote_port == c.remote_port && u.local_port == c.local_port &&
                u.state == c.state) { dup = true; break; }
        }
        if (!dup) uniq.push_back(c);
    }
    return uniq;
}

bool is_wifi() {
    // Cached: spawning PowerShell every frame caused severe UI lag. Refresh
    // at most once every 30 seconds.
    static bool cached = false;
    static bool result = false;
    static std::chrono::steady_clock::time_point last = {};
    auto now = std::chrono::steady_clock::now();
    if (cached && std::chrono::duration_cast<std::chrono::seconds>(now - last).count() < 30) {
        return result;
    }
    // Heuristic: any UP adapter's description contains "Wi-Fi"/"Wireless"/"802.11".
    std::string out = run_command(L"powershell", L"-NoProfile -Command \""
        L"Get-NetAdapter | Where-Object { $_.Status -eq 'Up' } | "
        L"Select-Object -ExpandProperty InterfaceDescription\"");
    std::string low = out;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    result = low.find("wi-fi") != std::string::npos ||
             low.find("wireless") != std::string::npos ||
             low.find("802.11") != std::string::npos;
    cached = true;
    last = now;
    return result;
}

std::pair<std::string, std::string> read_guest_dns() {
    std::string path = str::to_utf8(paths::install_root());
    // androws.conf lives under GameLoopData, not install root; resolve via known path.
    std::wstring wpath = paths::install_root();
    // Build the config path: GameLoopData is sibling of GameLoop under Tencent.
    // Fall back to scanning common locations.
    std::vector<std::wstring> candidates;
    // Try the documented location first.
    std::wstring base = paths::install_root();
    // install_root is ...\\Tencent\\GameLoop ; GameLoopData is ...\\Tencent\\GameLoopData
    if (!base.empty()) {
        auto pos = base.find(L"\\GameLoop");
        if (pos != std::wstring::npos) {
            std::wstring tencent = base.substr(0, pos);
            candidates.push_back(tencent + L"\\GameLoopData\\app\\config\\com.tencent.androws.conf");
        }
    }
    // Also try C: and D: Program Files.
    for (const auto* drive : {L"C:", L"D:"}) {
        candidates.push_back(std::wstring(drive) + L"\\Program Files\\Tencent\\GameLoopData\\app\\config\\com.tencent.androws.conf");
    }

    std::string text;
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (!f) continue;
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
        if (!text.empty()) break;
    }
    if (text.empty()) return {"", ""};

    // Find "dns":["...","..."]
    auto pos = text.find("\"dns\":[");
    if (pos == std::string::npos) pos = text.find("\"dns\": [");
    if (pos == std::string::npos) return {"", ""};
    auto end = text.find(']', pos);
    if (end == std::string::npos) return {"", ""};
    std::string arr = text.substr(pos, end - pos + 1);
    // Extract quoted strings.
    std::vector<std::string> ips;
    size_t i = 0;
    while ((i = arr.find('"', i)) != std::string::npos) {
        size_t j = arr.find('"', i + 1);
        if (j == std::string::npos) break;
        ips.push_back(arr.substr(i + 1, j - i - 1));
        i = j + 1;
    }
    if (ips.size() < 2) return {"", ""};
    return {ips[0], ips[1]};
}

std::vector<std::string> set_guest_dns(const std::string& dns1, const std::string& dns2) {
    std::vector<std::string> notes;

    // Find the file.
    std::vector<std::wstring> candidates;
    std::wstring base = paths::install_root();
    if (!base.empty()) {
        auto pos = base.find(L"\\GameLoop");
        if (pos != std::wstring::npos) {
            std::wstring tencent = base.substr(0, pos);
            candidates.push_back(tencent + L"\\GameLoopData\\app\\config\\com.tencent.androws.conf");
        }
    }
    for (const auto* drive : {L"C:", L"D:"}) {
        candidates.push_back(std::wstring(drive) + L"\\Program Files\\Tencent\\GameLoopData\\app\\config\\com.tencent.androws.conf");
    }

    std::wstring target;
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f) { target = c; break; }
    }
    if (target.empty()) {
        notes.push_back("androws.conf not found");
        return notes;
    }

    // Backup.
    std::wstring stamp;
    {
        SYSTEMTIME st;
        ::GetLocalTime(&st);
        wchar_t buf[32];
        std::swprintf(buf, 32, L"%04d%02d%02d-%02d%02d%02d",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        stamp = buf;
    }
    std::wstring bak = target + L".bak-" + stamp;
    // Copy file.
    std::ifstream src(target, std::ios::binary);
    std::ofstream dst(bak, std::ios::binary);
    dst << src.rdbuf();
    src.close();
    dst.close();
    notes.push_back("backup: " + str::to_utf8(bak));

    // Read, replace, validate.
    std::ifstream in(target, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    in.close();
    std::string text = ss.str();

    std::string old1 = "\"dns\":[\"114.114.114.114\",\"119.29.29.29\"]";
    std::string old2 = "\"dns\": [\"114.114.114.114\", \"119.29.29.29\"]";
    std::string old = text.find(old1) != std::string::npos ? old1 : old2;
    if (text.find(old) == std::string::npos) {
        // Try to find any dns array.
        auto pos = text.find("\"dns\"");
        if (pos != std::string::npos) {
            auto lb = text.find('[', pos);
            auto rb = text.find(']', lb);
            if (lb != std::string::npos && rb != std::string::npos) {
                old = text.substr(lb - 6, rb - lb + 7); // rough; fallback below
            }
        }
    }

    std::string newVal = "\"dns\":[\"" + dns1 + "\",\"" + dns2 + "\"]";
    // Do a targeted replace of the dns array regardless of old values.
    {
        auto pos = text.find("\"dns\"");
        if (pos == std::string::npos) {
            notes.push_back("dns field not found in androws.conf");
            return notes;
        }
        auto lb = text.find('[', pos);
        auto rb = text.find(']', lb);
        if (lb == std::string::npos || rb == std::string::npos) {
            notes.push_back("dns array malformed");
            return notes;
        }
        text.replace(lb, rb - lb + 1, "[\"" + dns1 + "\",\"" + dns2 + "\"]");
    }

    // Validate JSON.
    // Quick check: ensure the file still has the expected structure.
    if (text.find("\"dns\"") == std::string::npos) {
        notes.push_back("validation failed: dns field lost");
        return notes;
    }

    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();

    notes.push_back("guest DNS set to " + dns1 + " / " + dns2);
    notes.push_back("NOTE: GameLoop must be restarted for the guest to pick up the new DNS.");
    return notes;
}

} // namespace glt::core::netinfo