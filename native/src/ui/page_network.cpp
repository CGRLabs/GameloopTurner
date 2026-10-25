// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_network.cpp — Network & Servers page.
//
// Captures the guest's (Android) network connections via ADB and presents them
// with role labels, so the user can see which server they are on, whether it is
// reachable, and act on high-ping causes without leaving the tuner.
// ─────────────────────────────────────────────────────────────────────────────
#include "glt/ui/pages.hpp"

#include "glt/core/netinfo.hpp"
#include "glt/core/paths.hpp"
#include "glt/core/strings.hpp"
#include "glt/ui/theme.hpp"
#include "glt/ui/widgets.hpp"
#include "glt/ui/fonts.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

namespace glt::ui {
namespace {

ImVec4 col(theme::Rgb c) { return ImVec4(c.r, c.g, c.b, c.a); }

const char* role_short(const std::string& role) {
    if (role == "Game server") return "GAME SERVER";
    if (role == "Matchmaking / API") return "MATCHMAKING";
    if (role.rfind("Tencent", 0) == 0) return "TENCENT";
    if (role.rfind("Telemetry", 0) == 0) return "TELEMETRY";
    if (role == "DHCP gateway (host)") return "GATEWAY";
    if (role == "Local") return "LOCAL";
    return "UNKNOWN";
}

} // namespace

void draw_network(App& app) {
    theme::push_transparent();
    ImGui::BeginChild("##net_page", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginChild("##net_scroll", ImVec2(0, 0), ImGuiChildFlags_None);
    theme::pop_transparent();

    widgets::page_header(
        "Network & Servers",
        "Capture the guest's connections, identify your game server, and act on "
        "high-ping causes without leaving the tuner.");

    // ── Wi-Fi banner ────────────────────────────────────────────────────────
    if (glt::core::netinfo::is_wifi()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, col(theme::kWarnBg));
        ImGui::PushStyleColor(ImGuiCol_Border, col(theme::kWarn));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme::kCardRadius);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
        ImGui::BeginChild("##wifi_banner", ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::PushFont(fonts::current().body);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kWarn));
        ImGui::TextUnformatted("\xE2\x9A\xA0 You are on Wi-Fi. Wired Ethernet gives more "
                               "stable latency and lower jitter.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
        ImGui::Dummy(ImVec2(0, 6));
    }

    // ── Show raw toggle ──────────────────────────────────────────────────────
    static bool show_raw = false;
    ImGui::Checkbox("Show raw values (IPs, ports, state codes)", &show_raw);
    ImGui::Dummy(ImVec2(0, 6));

    // ── Action row ───────────────────────────────────────────────────────────
    const bool busy = app.busy() || app.network().busy;
    const float btn_h = 28.0f;
    const float btn_w = 150.0f;

    ImGui::BeginDisabled(busy);
    ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kAccent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kAccentHover));
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kOnAccent));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
    if (ImGui::Button("Refresh connections", ImVec2(btn_w, btn_h))) {
        app.refresh_network();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::SameLine(0, 8.0f);
    ImGui::BeginDisabled(busy);
    ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgActive));
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
    if (ImGui::Button("Flush DNS", ImVec2(btn_w, btn_h))) {
        app.post([] {
            const std::string out = glt::core::netinfo::flush_dns();
            (void)out;
        });
        app.log("Flushed DNS cache (ipconfig /flushdns).", log::Level::Info);
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    ImGui::SameLine(0, 8.0f);
    ImGui::BeginDisabled(busy);
    ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgActive));
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
    if (ImGui::Button("Open Res. Monitor", ImVec2(btn_w, btn_h))) {
        glt::core::netinfo::open_resource_monitor();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    if (app.network().busy) {
        ImGui::SameLine(0, 12.0f);
        ImGui::PushFont(fonts::current().small_text);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
        ImGui::TextUnformatted("Capturing guest connections via ADB...");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    ImGui::Dummy(ImVec2(0, 8));

    // ── Connections table ────────────────────────────────────────────────────
    theme::begin_card("Guest connections (captured via ADB)");
    widgets::help_marker("Connections are read from the guest (Android) via ADB on "
                         "127.0.0.1:5555. The game server is the high-port Tencent "
                         "Cloud IP that appears only during gameplay.");
    const auto& net = app.network();
    if (!net.error.empty()) {
        widgets::body_text("Capture failed: " + net.error, ImGui::GetContentRegionAvail().x,
                           col(theme::kError));
    } else if (net.conns.empty()) {
        widgets::body_text(
            "No connections captured yet. Click 'Refresh connections' while GameLoop "
            "and CODM are running. (ADB must be able to reach 127.0.0.1:5555.)",
            ImGui::GetContentRegionAvail().x, col(theme::kTextDim));
    } else {
        if (ImGui::BeginTable("##net_table", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Remote IP", ImGuiTableColumnFlags_WidthStretch, 0.22f);
            ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Proto", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthStretch, 0.20f);
            ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Reachable", ImGuiTableColumnFlags_WidthFixed, 80.0f);

            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableHeadersRow();

            for (const auto& c : net.conns) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(c.remote_ip.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", c.remote_port);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(c.proto.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(c.state.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::PushFont(fonts::current().small_text);
                ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
                ImGui::TextUnformatted(role_short(c.role));
                ImGui::PopStyleColor();
                ImGui::PopFont();
                ImGui::TableSetColumnIndex(5);
                if (c.ping_ms >= 0) ImGui::Text("%d ms", c.ping_ms);
                else ImGui::TextUnformatted("-");
                ImGui::TableSetColumnIndex(6);
                if (c.reachable) {
                    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kOk));
                    ImGui::TextUnformatted("yes");
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
                    ImGui::TextUnformatted("n/a");
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndTable();
        }
    }
    theme::end_card();

    if (show_raw && !net.conns.empty()) {
        theme::begin_card("Raw connection dump");
        ImGui::PushFont(fonts::current().small_text);
        for (const auto& c : net.conns) {
            ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
            ImGui::Text("local=%s:%d  remote=%s:%d  proto=%s  state=%s  role=%s  ping=%d",
                        c.local_ip.c_str(), c.local_port, c.remote_ip.c_str(),
                        c.remote_port, c.proto.c_str(), c.state.c_str(),
                        c.role.c_str(), c.ping_ms);
            ImGui::PopStyleColor();
        }
        ImGui::PopFont();
        theme::end_card();
        ImGui::Dummy(ImVec2(0, 8));
    }

    // ── Game server highlight ────────────────────────────────────────────────
    theme::begin_card("Game server");
    widgets::help_marker("The server your current match is running on. Usually Tencent "
                         "Cloud (US West Coast). High ping here is physics (distance) "
                         "or ISP routing, not a local setting.");
    const auto& conns = net.conns;
    const glt::core::netinfo::Connection* game = nullptr;
    for (const auto& c : conns) {
        if (c.role == "Game server") { game = &c; break; }
    }
    if (game) {
        widgets::kv_row("Remote IP", game->remote_ip);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d", game->remote_port);
        widgets::kv_row("Port", buf);
        widgets::kv_row("Protocol", game->proto);
        widgets::kv_row("State", game->state);
        if (game->ping_ms >= 0) {
            std::snprintf(buf, sizeof(buf), "%d ms", game->ping_ms);
            widgets::kv_row("Ping", buf);
        } else {
            widgets::kv_row("Ping", "not measured");
        }
        widgets::body_text(
            "This is the server your current match is running on. It is a Tencent Cloud "
            "PoP (Santa Clara, CA for most regions). High ping here is usually physics "
            "(distance) or ISP routing, not a local setting.",
            ImGui::GetContentRegionAvail().x, col(theme::kTextDim));
    } else {
        widgets::body_text(
            "No game server detected. Start a match in CODM, then click "
            "'Refresh connections'.",
            ImGui::GetContentRegionAvail().x, col(theme::kTextDim));
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 8));

    // ── DNS override (guest) ─────────────────────────────────────────────────
    theme::begin_card("Guest DNS override (androws.conf)");
    widgets::help_marker("The DNS resolver the emulator uses. 114.114.114.114 is "
                         "unreachable from Algeria (100% packet loss). Change to "
                         "1.1.1.1 / 8.8.8.8 for faster resolution. GameLoop must "
                         "be restarted for the guest to pick up the new DNS.");
    {
        static char dns1[64] = "1.1.1.1";
        static char dns2[64] = "8.8.8.8";
        static bool inited = false;
        static std::string cached1, cached2;
        static std::chrono::steady_clock::time_point last_read{};
        auto nowr = std::chrono::steady_clock::now();
        if (!inited || std::chrono::duration_cast<std::chrono::seconds>(nowr - last_read).count() >= 5) {
            auto cur = glt::core::netinfo::read_guest_dns();
            cached1 = cur.first;
            cached2 = cur.second;
            last_read = nowr;
            inited = true;
            if (!cached1.empty()) {
                std::snprintf(dns1, sizeof(dns1), "%s", cached1.c_str());
                std::snprintf(dns2, sizeof(dns2), "%s", cached2.c_str());
            }
        }

        ImGui::PushFont(fonts::current().small_text);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextMuted));
        ImGui::TextUnformatted("Current guest resolvers (read from androws.conf):");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        char cur1[64], cur2[64];
        std::snprintf(cur1, sizeof(cur1), "%s", cached1.empty() ? "?" : cached1.c_str());
        std::snprintf(cur2, sizeof(cur2), "%s", cached2.empty() ? "?" : cached2.c_str());
        widgets::kv_row("DNS 1", cur1);
        widgets::kv_row("DNS 2", cur2);

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("Primary", dns1, sizeof(dns1));
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputText("Secondary", dns2, sizeof(dns2));

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
        ImGui::PushFont(fonts::current().small_text);
        ImGui::TextUnformatted("Applies to androws.conf (backed up first). "
                              "GameLoop must be restarted for the guest to pick up the new DNS.");
        ImGui::PopFont();
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::BeginDisabled(busy);
        ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kAccent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kAccentHover));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kOnAccent));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
        if (ImGui::Button("Apply DNS", ImVec2(120.0f, 26.0f))) {
            std::string a(dns1), b(dns2);
            app.post([&app, a, b] {
                try {
                    auto notes = glt::core::netinfo::set_guest_dns(a, b);
                    for (const auto& n : notes) app.log(n, log::Level::Info);
                    app.log("Guest DNS updated. Restart GameLoop.", log::Level::Success);
                    app.toast("Guest DNS updated", log::Level::Success);
                } catch (const std::exception& e) {
                    app.log(std::string("DNS update failed: ") + e.what(), log::Level::Error);
                    app.toast("DNS update failed", log::Level::Error);
                }
            });
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();

        ImGui::SameLine(0, 8.0f);
        ImGui::BeginDisabled(busy);
        ImGui::PushStyleColor(ImGuiCol_Button, col(theme::kBgHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme::kBgActive));
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextPrimary));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme::kCardRadius);
        if (ImGui::Button("Reset to 114/119", ImVec2(140.0f, 26.0f))) {
            std::snprintf(dns1, sizeof(dns1), "%s", "114.114.114.114");
            std::snprintf(dns2, sizeof(dns2), "%s", "119.29.29.29");
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 8));

    // ── Live ping (non-blocking, measured during Refresh) ────────────────────
    theme::begin_card("Live ping (1.1.1.1)");
    widgets::help_marker("Ping is measured in the background during 'Refresh "
                         "connections' (no UI freeze). For continuous real-time "
                         "monitoring use Monitor-Jitter.ps1 -Target <ip>.");
    {
        const auto& n = app.network();
        // Show the game server ping if available.
        int game_ping = -1;
        for (const auto& c : n.conns) {
            if (c.role == "Game server" && c.ping_ms >= 0) { game_ping = c.ping_ms; break; }
        }
        char buf[64];
        if (game_ping >= 0) std::snprintf(buf, sizeof(buf), "Game server: %d ms", game_ping);
        else std::snprintf(buf, sizeof(buf), "Game server: not measured");
        widgets::kv_row("Ping", buf);

        // Show unique remote IPs with their ping.
        std::vector<std::string> seen;
        for (const auto& c : n.conns) {
            if (c.remote_ip.empty() || c.remote_ip == "0.0.0.0" ||
                c.remote_ip == "127.0.0.1") continue;
            bool dup = false;
            for (const auto& s : seen) if (s == c.remote_ip) { dup = true; break; }
            if (dup) continue;
            seen.push_back(c.remote_ip);
            char pbuf[64];
            if (c.ping_ms >= 0) std::snprintf(pbuf, sizeof(pbuf), "%d ms", c.ping_ms);
            else std::snprintf(pbuf, sizeof(pbuf), "unreachable");
            widgets::kv_row(c.remote_ip.c_str(), pbuf);
        }

        if (n.conns.empty()) {
            widgets::body_text("Click 'Refresh connections' to measure ping to each "
                               "endpoint (runs in background, no UI freeze).",
                               ImGui::GetContentRegionAvail().x, col(theme::kTextDim));
        }

        ImGui::PushFont(fonts::current().small_text);
        ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
        ImGui::TextUnformatted("For continuous real-time monitoring, use "
                               "Monitor-Jitter.ps1 -Target <game server IP>.");
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 8));

    // ── High-ping help ───────────────────────────────────────────────────────
    theme::begin_card("High-ping help (read-only guidance)");
    widgets::help_marker("Most high-ping causes are NOT fixable by a setting. Work "
                         "through these in order. See docs/10-testing-plan.md for "
                         "the full test matrix.");
    widgets::body_text(
        "Most high-ping causes are NOT fixable by a setting. Work through these in order:",
        ImGui::GetContentRegionAvail().x, col(theme::kTextPrimary));
    ImGui::Dummy(ImVec2(0, 4));

    const char* tips[] = {
        "1. Check the game server above - if it is far away (e.g. US West from Algeria), "
            "that is physics. No local tweak fixes distance.",
        "2. Server region: pick the nearest region in CODM settings. If CODM auto-selected "
            "a far region, force the closest one.",
        "3. Wired > Wi-Fi: plug in Ethernet. Wi-Fi jitter is the #1 cause of 'high ping' "
            "that feels worse than the number suggests.",
        "4. Bufferbloat: run a download while pinging. If ping spikes, enable SQM/QoS on "
            "your router (fq_codel / Cake).",
        "5. Kill background hogs: pause Windows Update, OneDrive, cloud sync, and other "
            "launchers during matches.",
        "6. DNS: your guest DNS was 114.114.114.114 (dead from Algeria). It has been "
            "changed to 1.1.1.1 / 8.8.8.8 in androws.conf.",
        "7. VPN / TUN adapters: a leftover tunnel can capture GameLoop traffic. Uninstall "
            "unused VPNs fully (not just disconnect).",
        "8. GameLoop priority: VM & Priority page -> Apply All (resets on exit).",
        "9. Do NOT block game-server or matchmaking endpoints above - that disconnects "
            "you. Only telemetry endpoints are safe to block.",
    };
    for (const char* t : tips) {
        widgets::body_text(t, ImGui::GetContentRegionAvail().x, col(theme::kTextMuted));
        ImGui::Dummy(ImVec2(0, 2));
    }

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushFont(fonts::current().small_text);
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme::kTextDim));
    ImGui::TextUnformatted("Tools: Resource Monitor (resmon) shows per-process network. "
                           "PingPlotter graphs jitter/loss over time.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    theme::end_card();

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::EndChild();
    ImGui::EndChild();
}

} // namespace glt::ui