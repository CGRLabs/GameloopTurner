# 09 — Knowledge map: what to monitor and what to modify

Companion to docs 01–08 (live PC forensics) and `keymap-auto-switch.md` (mechanism).
This file is the **index**: every surface GameLoop/CODM exposes on this PC, what it
does, whether to monitor or modify it, and the risk. Read top-to-bottom once; after
that, jump by section.

Conventions:
- **M** = monitor only, **T** = tweakable, **⚠** = risky / backup first.
- Paths use `%APPDATA%` = `C:\Users\SKULL\AppData\Roaming`, reg = `HKCU\Software\Tencent\GameLoop`.

---

## 1. Where things live (don't memorise — just reference)

| Surface | Path |
|---|---|
| Install (live) | `D:\Program Files\Tencent\GameLoop\Application\7.0.167.0\` |
| App data | `%APPDATA%\Tencent\GameLoop\` |
| Master DB | `%APPDATA%\Tencent\GameLoop\db\Configs.db` (+ `-wal`/`-shm`) |
| Smart-key file | `%APPDATA%\Tencent\GameLoop\config\com.activision.callofduty.shooter\smk.conf` |
| Engine JSONs | `D:\Program Files\Tencent\GameLoop\Application\7.0.167.0\{Androws,deviceConfig}.json` |
| Engine config | `D:\Program Files\Tencent\GameLoopData\app\config\com.tencent.androws.conf` |
| CODM guest flags | `…\app\config\com.activision.callofduty.shooter.conf` |
| Registry | `HKCU\Software\Tencent\GameLoop` + `HKLM\SOFTWARE\WOW6432Node\Tencent\GameLoop` |
| Tuner binary | `C:\Users\SKULL\Desktop\GameLoopTuner.exe` (release, 1.75 MB) |
| Backups | `%APPDATA%\Tencent\GameLoop\db\Configs.db.bak-*` and `…\smk.conf.bak-*` |
| Quarantine | `…\Application\7.0.167.0\ano_tmp_quarantine\` |
| Keymap exports | `D:\CODM-Assets\NOTES\*.txt` + `GameLoopHub-BR-Global.zip` |
| Recordings | `D:\CODVIDS\2025\` + `2026\` (DO-NOT-DELETE) |

---

## 2. What the tuner already monitors (live, every poll)

Drift keys (11): `cores`, `mem_gb`, `lastmodeid`, `flat_145_all`, `flat_146`,
`flat_531686`, `clarity`, `low_latency_mode`, `default_frame_gen_on`,
`file_tex_clean`, `smk_readonly`.

Dashboard cards (6): processes, VM allocation, active keymap, CODM settings,
drive headroom, service state. Plus: running process table, scrubbed log (500-line
ring), backups list, diagnostic report (200 KB cap, secrets stripped).

---

## 3. Modifiable surfaces by goal

### 3.1 Performance (render / VM / CPU)

| Item | Where | What it does | Risk |
|---|---|---|---|
| VM cores / RAM | reg `Settings\Common` | Slices CPU/RAM to the VM. Tuner caps at host−1 / host−2. | T |
| Clarity tier (1/2/3) | DB `local_user_config.clarity` | Render-resolution tier. Tier 2 = clearer + more FPS here; tier 1 was *laggier* (shifts work to CPU). | T |
| Low-latency | DB `local_user_config.low_latency_mode` | Smaller frame queue = best feel-per-cost. Keep ON. | T |
| Frame-gen | DB `local_user_config.default_frame_gen_on` | Fake frames add lag. Keep OFF (real 120 > fake 240). | T |
| Frame rate cap | DB `local_user_config.frame_rate` + host_config `default/maximum_frame_rate` | Real cap. 120 competitive; 240 allowed but rarely real. | T |
| Process priorities | live PIDs (tuner) | Emulator+VM→High, UI→High, Assistant→Above, DldSvr→Below. Resets on exit. | T |
| `DisableFullscreenOptimization` | reg `Settings\Common` (`0`) | Windows FSO. Try 1 if fullscreen feels laggy. | T |
| `RenderMode` / `RenderUserType` | reg `Settings\Common` | Display only — engine picks per map. Forcing can crash shaders. | ⚠ |
| `ShowFps` | reg `Settings\GameLoop` (`0`) | In-emulator FPS counter. Flip to 1 while tuning. | T |
| `GameMode` | reg `Settings\GameLoop` (`0`) | Windows Game Mode. | T |
| WheelScroll bezier params | reg `WheelScroll` | Anti-jitter scroll. If wheel-aim feels laggy, these are the suspect. | T |
| `softvideo` | DB guest_config (`true`) | CPU decode of flat video (lobby/killcam) only — not 3D. Leave ON. | ⚠ |
| `tp` (texture pipe) | DB host_config (`true`) | Leave on. | ⚠ |
| Power plan / NIC tuning | Windows | High Performance plan; disable NIC power management. | T |
| `installedPkgAutoClearSwitch` | reg (`1`) | Clears xapk cache post-install (saves D:). | T |

### 3.2 Input / aim / keymaps

| Item | Where | What it does | Risk |
|---|---|---|---|
| Active mode pointer | DB `lastmodeid` | Which mode is active (pinned to 145). Engine re-points at runtime — harmless noise. | T |
| User blobs (flat) | DB `keymap_data.unlogin.*` | The actual bindings. Tuner flattens these to 0 rules. | T |
| Official blobs | DB `offcial_keymap_data2.*` | Trigger-guarded source. Only overwrite content (plan B) if DMZ/BR reappear. | ⚠ |
| Jump dictionary | DB `offcial_config2` (Switch→Texture) | Read-only via trigger. | ⚠ |
| smk Tex templates | file + DB mirror | Recognition data. Blinding stops auto-switch. | ⚠ |
| `KeyMapInputType` | reg (`1`) | Keyboard input mode. | T |
| `scenes_config` | DB row 426 (`{"scenes":[]}`) | Custom keymap scenes — empty on this PC. | T |
| NOTES exports | `D:\CODM-Assets\NOTES\*.txt` | Human backups of keymap blobs; recovery path. | M |

### 3.3 Display / window

| Item | Where | What it does |
|---|---|---|
| Render window size | DB `local_user_config.landscape_render_wnd_size` (`[916,515]`) | Render window inside monitor box. |
| Monitor size / DPI | DB `local_user_config.monitor_size` / `monitor_dpi` | |
| Aspect ratios | DB `local_user_config.aspect_ratio` / `special_aspect_ratio` | |
| `auto_fullscreen`, `follow_screen_aspect_ratio` | DB host_config | |
| `is_use_automatic_resolution` | DB host_config (`1`) | |
| Fullscreen toast (F11) | reg `Mark\full_screen_toast_mark_…` | One-shot; won't nag again. |

### 3.4 Audio

| Item | Where | What it does |
|---|---|---|
| `AndroidSystemVolume` | reg (`80`) | Emulator media volume. |
| Output/Input device GUIDs | reg (empty = default) | Pin a specific audio device. |
| `RecordAudioSwitch` | reg (`0`) | Mic into screen recordings. |

### 3.5 Storage / disk

| Item | Where | What it does |
|---|---|---|
| D: free space | live: 86 GB | Engine wants ≥10 GB. Now fine. |
| `required_disk_space` | androws.conf (`10` GB) | Engine's own threshold. |
| `installedPkgAutoClearSwitch` | reg (`1`) | Clears xapk cache post-install. |
| `CustomizedPkgSavePath` | reg (empty) | Move GameLoopData off D: if needed. |
| VHD | `Image\data.vhd` (11.6 GB) | Grows with Android data; compact if large. |
| `D:\GameLoop_Backup_20261014_221627` | disk | Reclaimable full backup from 10/14 upgrade. |

### 3.6 Network (see §4, and `11-connection-reference.md`)

| Item | Where | What it does |
|---|---|---|
| DNS resolvers | androws.conf (`114.114.114.114`, `119.29.29.29`) | Chinese DNS — 114 is **dead from Algeria** (100% loss). Override at Windows adapter. |
| `XyAcc` / `LeigodAcc` | firewall Allow rules | Network-boost proxies; can add latency if node is bad. Not currently installed. |
| ADB `0.0.0.0:5555` | GameLoopEmulator | LAN-reachable debug bridge (security surface). Used by `Get-CodmConnections.ps1`. |
| `AndroidAdbMode` | reg (`0`) | Restricted mode. |
| Guest game server | `/proc/net/tcp` (via ADB) | Tencent Cloud US West (Santa Clara) — see `11-connection-reference.md`. |
| Guest matchmaking API | `/proc/net/tcp` (via ADB) | `49.51.67.202:443` (HTTPS). |
| Guest telemetry | `/proc/net/tcp` (via ADB) | Google `172.217.119.4:443` (CLOSE_WAIT). |

### 3.7 Anti-detection / compatibility (⚠)

| Item | Where | Why leave it |
|---|---|---|
| `named_filter` / `vattr_list` | androws.conf | Hides qemu/vbox paths from apps. Touching breaks anti-detection. |
| `mock telephony/hardware` | androws.conf | Emulator spoofing. |
| `dpiPatch` | androws.conf | |
| `gpu_blacklist.json` / `fg_gpu_whitelist.json` | renderer/ | RX 5700 XT already allowed (GL 4.6 + Vulkan). |
| Device cert | `config\7f18….dat` + reg `cert_hash` | Loss forces re-login + re-attestation. |
| `sdid` / `vdid` / `oaid` | DB `androws.sdid` | Device/account fingerprint. Don't share. |
| `Env\V*Dioa` blobs, `qimei` | reg / appdata | Login attestation. |

### 3.8 Telemetry / housekeeping

| Item | Where | What it does |
|---|---|---|
| Beacon logs | `ams_cache\AmsWinSdk_*.log` | First place to look on "login failed". |
| `Duration\*` | reg | "Last did X at" ledger. |
| `Mark\*` | reg | One-shot flags (e.g. fullscreen toast). |
| `LaunchDebounce` | reg | Anti-double-click guard. |
| `RunningAppInfo` | reg | Which app is running. |
| Builds | `Application\7.0.167.0` | Newer builds land here via AutoSilentUpdate. |

### 3.9 Service / autostart

| Item | Where | What it does |
|---|---|---|
| `GameLoopService` start type | SCM (Automatic) | `sc config GameLoopService start=demand` stops auto-start. |
| Scheduled tasks / Startup / IFEO | verified empty | Autostart is purely the service. |
| Firewall rules | 10 inbound Allow | Normal set; two `*Acc` belong to accelerators. |

---

## 4. Network / high ping — alternatives to the heavy `netsh` reset

Goal: fix high ping **without** `netsh winsock reset` / `int ip reset` (which need a reboot
and wipe the stack). Try these first, lightest → heavier.

### 4.1 Lightest (no reboot, no risk)

1. **`ipconfig /flushdns`** — often all "refresh net" was doing. Clears stale DNS cache.
2. **Restart the DNS client service:** `Restart-Service Dnscache` (Admin). Lighter than a full reset.
3. **Adapter toggle:** disable then re-enable the NIC in Settings/Device Manager. Re-initialises the adapter without a reboot.
4. **Airplane-mode toggle** (if available) — same effect, even lighter.
5. **Renew IP only:** `ipconfig /release` then `ipconfig /renew` on the specific adapter (no reboot).
6. **Clear ARP cache:** `arp -d *` — fixes stale neighbour entries on the LAN.
7. **Change DNS at the adapter** (see 4.2) — no restart needed, takes effect on next query.

### 4.2 Fix the DNS (highest impact here)

Your `androws.conf` hardcodes `114.114.114.114` + `119.29.29.29` (Chinese resolvers).
From a non-China location these can be slow/unreachable, and CODM matchmaking depends on
fast DNS.

- **Do not edit androws.conf.** Override at the Windows adapter:
  Settings → Network → adapter → DNS → `1.1.1.1` / `8.8.8.8` (or your ISP's).
- Then `ipconfig /flushdns`.
- Test: `Measure-Command { Resolve-DnsName activision.com }`.

### 4.3 GameLoop accelerators (XyAcc / LeigodAcc)

Firewall has Allow rules for both — they're network-boost proxies that route game traffic
through a selected node.

- They help **only** if the node is good; otherwise they add latency + loss.
- Test: toggle the accelerator in GameLoop settings, or temporarily block those
  executables in the firewall, and re-check ping.
- If ping improves with it off, the node selection was the culprit.
- Live check 2026-10-18: no `XyAcc*`/`LeigodAcc*` files exist anywhere on C: or D:.
  They are not installed locally — downloaded on demand when the accelerator is
  enabled in GameLoop, or the feature is off. The firewall Allow rules exist
  regardless, so their presence alone doesn't mean the accelerator is active.

### 4.4 VPN / proxy / TUN adapters

- A "disabled" VPN or a leftover TUN adapter (WireGuard/Clash/v2ray) can still capture
  GameLoop traffic.
- Test with all VPN clients **fully uninstalled** (not just disconnected) to isolate.

### 4.5 Windows tuning (no reboot where noted)

- Set GameLoop processes to High priority (tuner VM & Priority page — live).
- QoS: `gpedit.msc → QoS Packet Scheduler → Limit reservable bandwidth → 0`.
- NIC driver update; disable "Interrupt Moderation" if latency-bound.
- Disable Large Send Offload (LSO) on the adapter if small-packet latency is high.
- Wired Ethernet over Wi-Fi; 5 GHz, close to router, avoid 2.4 GHz congestion.

### 4.6 In-game / emulator side

- `low_latency_mode` ON (smaller frame queue — best feel-per-cost).
- Frame rate 120 (real) > fake 240 — keep frame-gen OFF (already set).
- CODM server region: nearest region; far-region ping is a matchmaking issue, not local.

### 4.7 Diagnose before fixing

```powershell
ping -n 20 192.168.1.1          # gateway
ping -n 20 1.1.1.1              # public
pathping <server_ip>             # from netstat while in game
tracert -d <server_ip>
Measure-Command { Resolve-DnsName activision.com } | Select TotalMilliseconds
Get-NetAdapter | Where Status -eq Up | Select Name, InterfaceDescription, LinkSpeed
Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' |
  Select ProxyEnable, ProxyServer
```

Look for: loss to gateway = local/Wi-Fi; loss past a hop = ISP/routing; slow DNS =
resolver (likely here).

### 4.8 Heavy option (last resort, needs reboot)

`ipconfig /flushdns; ipconfig /release; ipconfig /renew; netsh winsock reset;
netsh int ip reset; netsh advfirewall reset` — only if the above fail. Re-add
GameLoop firewall Allow rules after `advfirewall reset`.

---

## 5. Do-not-touch (backup first or don't at all)

- `smk` blobs, `sdid`/`vdid`/`cert`/`oaid`, `named_filter`/`vattr_list`, DNS inside
  androws.conf, device cert `config\7f18….dat`, `offcial_keymap_data2.*` rows
  (trigger-guarded), `named_filter`/`vattr_list`, GPU blacklist, `qimei`/`V*Dioa` blobs.
- Never hand-edit `smk.conf` without: unlock (clear RO) → write → relock. Backup first.

---

## 6. Live snapshot (verified 2026-10-18)

| Item | Value |
|---|---|
| GameLoop | 7.0.167.0 (installed 2026/10/14), engine 5.10.7000.123 |
| Processes | 6 running; Vm 551 MB WS |
| Service | GameLoopService, Automatic, Running |
| D: free | 86 GB / 654 GB (docs said 0 — stale) |
| C: free | 114 GB / 300 GB |
| Registry cores / mem / mode | 4 / 10 / 0 (docs said 6 / 10 / 3) |
| BossKey | 0 (docs said 1) |
| IP geo | 36.08 / 3.08 (docs said 28.02 / 1.65) |
| smk.conf | read-only, 182 templates, all 4 blind IDs absent |
| User blobs | all 5 = 0 SwitchOperation (flat) |
| lastmodeid | 145; recentlyused `{"145":…}` |
| local_user_config | clarity 2, frame_rate 120 (low_latency/frame_gen keys absent now) |
| host_config | default/max fps 120/120 (docs said 120/240) |
| hosts block | not applied (experimental) |
| Engine JSONs | now under `Application\7.0.167.0\` (docs path stale) |
| Tuner binary | `Desktop\GameLoopTuner.exe` (release) + workspace debug build |

Conclusion: the **fix chain is holding** (`FullChain` would report applied). The docs
01–08 are a 2026-09-20 snapshot; live values moved on the 10/14 upgrade. Prefer the
tuner's Dashboard + diagnostic report as the live source of truth.

---

## 7. Quick reference

- Package: `com.activision.callofduty.shooter`
- Modes: 145 (MP), 146 (BR), 531686 (DMZ)
- Flat solution IDs: 145→`1792013098464` (mpkeymap), 146→`146`, 531686→`1063553`
- Blind Tex IDs: 10004, 10046, 30004, 30046
- Registry base: `HKCU\Software\Tencent\GameLoop`; settings under `Settings\Common` + `Settings\GameLoop`
- Backup pattern: `Configs.db.bak-YYYYMMDD-HHMMSS`, `smk.conf.bak-YYYYMMDD-HHMMSS`
- Tuner: 7 pages (Dashboard, Keymaps, Performance, VM & Priority, Backups, Logs, About)
- Drift poll: 1 s slices, 32-entry queue cap

---

*Sources: docs 01–08, `keymap-auto-switch.md`, `cxx-port-audit.md`, native C++ source
(`native/src/core/*`, `native/src/ui/*`), and live probes on 2026-10-18 (processes,
registry, DB, smk.conf, disk, engine JSONs). Sensitive values (seed_id, vdid, oaid,
cert_hash, coords) are intentionally not reproduced here.*