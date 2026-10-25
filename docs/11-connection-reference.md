# 11 — Connection reference: what each GameLoop/CODM connection is for

Captured live 2026-10-18 via ADB (`/proc/net/tcp` + `/proc/net/udp` on the guest)
and ipinfo.io geo lookup. Use this as the reference for a future **Network** page/card
in the tuner, so users can see what they're connected to and what each endpoint does.

## 1. How to capture (for users / for the tuner)

```powershell
# List guest connections:
powershell -ExecutionPolicy Bypass -File scripts-ps\Get-CodmConnections.ps1

# Identify the game server: run during a match vs in lobby, diff the output.
# The IP that appears only during gameplay (high port) is the game server.
```

The tuner could automate this by calling ADB internally (like it already resolves
paths) and parsing `/proc/net/tcp` + `/proc/net/udp` — the same code shape as
`paths::adb_exe()` + a small decoder.

## 2. Connections found (live, your machine)

| Remote IP | Port | Proto | State | UID | Org / Geo | Likely role |
|---|---|---|---|---|---|---|
| 43.130.54.101 | 65050 | TCP | ESTABLISHED | 10075 (CODM) | Tencent Cloud, Santa Clara CA | **Game server** (high port, gameplay) |
| 43.130.19.154 | 65010 | TCP | ESTABLISHED | 10075 (CODM) | Tencent Cloud, Santa Clara CA | **Game server** (high port, gameplay) |
| 49.51.67.28 | 5692 | TCP | ESTABLISHED | 10075 (CODM) | Tencent Cloud, Santa Clara CA | **Game server / match relay** (high port) |
| 49.51.67.202 | 443 | TCP | ESTABLISHED | 10075 (CODM) | Tencent Cloud, Santa Clara CA | **Matchmaking / login / API** (HTTPS) |
| 172.217.119.4 | 443 | TCP | CLOSE_WAIT | 10050 (other) | Google | **Telemetry / analytics** (Google SDK) |
| 172.17.224.1 | 67 | UDP | (DHCP) | — | host vSwitch | **DHCP/DNS gateway** (host-side) |

Notes:
- All four CODM endpoints are **Tencent Cloud, Santa Clara CA (US West)** — AS132203.
  Even though you select "EU" in CODM, the actual server IPs resolve to Tencent's US
  West Coast PoP. This is why ping from Algeria is ~40–60 ms minimum (physics).
- UID 10075 = the CODM app process inside Android. UID 10050 = a different app/SDK
  (likely Google Play Services / Firebase telemetry).
- The guest IP is `172.17.225.41/20`; host vSwitch is `172.17.224.1`.

## 3. What each connection is (for users)

Surface this in the tuner as a "Connections" card with a one-line description per row:

| Endpoint | Plain-English meaning | Safe to block? |
|---|---|---|
| `43.130.x.x:65xxx` / `49.51.67.x:5xxx` | **Game server** — the match you're playing in right now. Carries gameplay state (movement, shots, hit registration). | **No** — blocking = disconnect/kick. |
| `49.51.67.x:443` | **Matchmaking / login / profile API** — finds matches, logs you in, syncs stats. | **No** — blocking = can't queue or log in. |
| `172.217.x.x:443` (Google) | **Telemetry / crash reporting / ads SDK** — sends anonymous usage/crash data. | Yes (privacy) — but may break some features. |
| `172.17.224.1:67` (DHCP) | **Local gateway** — host provides IP/DNS to the guest. | **No** — internal only. |
| `GameLoopDldSvr` (localhost :58320) | **Local update server** — serves game updates from disk. | **No** — needed for updates. |

## 4. What the tuner could surface (proposed feature)

A new **Network** card on the Dashboard (or a dedicated page) showing:

- **Connected game server** + geo (city/country) + ping (live, via ICMP).
- **Matchmaking endpoint** status (reachable / latency).
- **Telemetry toggle** (block/unblock the Google/SDK endpoints) — privacy feature.
- **Connection count** (established TCP/UDP) — spike = background app woke up.
- **Loss/jitter** to the game server (rolling, from the Monitor-Jitter logic).
- **DNS resolver in use** (guest) — flag if it's a dead Chinese resolver (114).
- **Server region** detected from the game server IP (map IP → region).

This turns "I have high ping" from a mystery into a readable state: users see *which*
server they're on, *where* it is, and *whether* it's reachable.

## 5. Safety rules for the tuner

- **Never block game-server or matchmaking endpoints** — disconnects the player.
- **Telemetry endpoints are the only safe-to-block** category, and even then warn
  that some features (crash reporting, login via Google) may break.
- **Don't auto-block based on geo** — a US server might be the correct one for a
  region (Tencent routes some "EU" traffic through US West). Let the user decide.
- **Always show the IP + geo + ping** so the user can make an informed choice.
- **Don't store/share server IPs** in reports without scrubbing? They're not
  secrets, but keep them out of public diagnostic reports by default (or make it
  opt-in) to avoid leaking infrastructure details.

## 6. How to identify the game server (for users)

The high-port Tencent IPs (65050, 65010, 5692) appear only during gameplay. To
confirm which one is *your* game server:

1. Run `Get-CodmConnections.ps1` in the lobby → note the IP list.
2. Start a match → run it again → the new high-port IP is your game server.
3. `Test-Connection <ip> -Count 10` → compare to in-game ping.
4. Pass it to `Monitor-Jitter.ps1 -Target <ip>` or `Monitor-Connection.ps1 -ServerIP <ip>`.

## 7. Known limitation

Game traffic is UDP and often connectionless, so `/proc/net/udp` only shows
"established" sockets (state 01). The game server may use UDP without a persistent
entry — in that case, the TCP entries (matchmaking/login) are the only visible
ones, and the game server IP must be captured via Wireshark on the host virtual
adapter or from in-game network stats.

---

*Sources: live ADB capture 2026-10-18, ipinfo.io geo, CODM/GameLoop documentation,
and `docs/09-knowledge-map.md` §3.6/§4.*