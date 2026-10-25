#pragma once

#include <string>
#include <utility>
#include <vector>

namespace glt::core::netinfo {

struct Connection {
    std::string local_ip;
    int local_port = 0;
    std::string remote_ip;
    int remote_port = 0;
    std::string proto;   // "TCP" / "UDP"
    std::string state;   // "ESTABLISHED" / "LISTEN" / ...
    std::string role;    // human-readable role
    int ping_ms = -1;    // -1 = not measured / unreachable
    bool reachable = false;
};

// Runs adb against the GameLoop emulator and parses /proc/net/tcp + udp.
// Returns an empty vector if adb is missing or the emulator is not running.
std::vector<Connection> capture();

// Heuristic role label from IP + port (see docs/11-connection-reference.md).
std::string role_for(const std::string& ip, int port);

// Measures ICMP round-trip to `ip`. Returns -1 on failure/timeout.
int ping_ms(const std::string& ip);

// Runs "ipconfig /flushdns". Best-effort, returns the command output.
std::string flush_dns();

// Opens Windows Resource Monitor.
void open_resource_monitor();

// True if the default internet connection is Wi-Fi.
bool is_wifi();

// Read the guest DNS resolvers from androws.conf. Returns {"", ""} on failure.
std::pair<std::string, std::string> read_guest_dns();

// Replace the guest DNS resolvers in androws.conf (backs up first).
// Returns a vector of log lines. Throws on failure.
std::vector<std::string> set_guest_dns(const std::string& dns1, const std::string& dns2);

} // namespace glt::core::netinfo