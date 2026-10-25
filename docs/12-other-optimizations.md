# 12 — Other optimization avenues (beyond keymaps & network)

Companion to docs 09–11. Catalog of every other lever that can affect GameLoop/CODM
performance, feel, or stability — what it does, where it lives, whether it's in the
tuner, and whether it's worth exposing. Use this as the roadmap for future tuner
features.

---

## Tier 1 — High impact, low effort (mostly already in tuner)

| # | Lever | Where | What it does | In tuner? |
|---|---|---|---|---|
| 1 | VM cores / RAM | reg `Settings\Common` | Slices host resources to the VM. | ✅ Performance page |
| 2 | Clarity tier | DB `local_user_config.clarity` | Render resolution. Tier 2 = clearer + more FPS here. | ✅ Performance page |
| 3 | Low-latency | DB `local_user_config.low_latency_mode` | Smaller frame queue. Keep ON. | ✅ Performance page |
| 4 | Frame-gen | DB `local_user_config.default_frame_gen_on` | Fake frames add lag. Keep OFF. | ✅ Performance page |
| 5 | Process priorities | live PIDs | Emulator+VM→High. Resets on exit. | ✅ VM & Priority page |
| 6 | Keymap auto-switch fix | DB + smk.conf + ano_tmp | Stops layout from flipping mid-match. | ✅ Keymaps page (core feature) |
| 7 | DNS override (guest) | androws.conf `dns` | 114 is dead from Algeria; changed to 1.1.1.1/8.8.8.8. | ❌ manual edit (could add toggle) |
| 8 | Wired Ethernet | hardware | Biggest single stability win if on Wi-Fi. | ❌ user action |
| 9 | Background hogs | Windows | Pause updates/sync/downloads during matches. | ❌ user action |
| 10 | Power plan | Windows (`powercfg`) | High Performance while gaming. | ❌ could add button |

## Tier 2 — Medium impact, moderate effort

### 2.1 Input / aim feel

| Lever | Where | Notes |
|---|---|---|
| Mouse sensitivity (in-game) | CODM settings | Per-scope sensitivity. Lower = more precise, higher = faster turns. |
| Gyro | CODM settings | Tilt-to-aim. Huge for mobile players; on PC via emulator tilt simulation. |
| Aim assist | CODM settings | On/off, strength. Affects tracking. |
| Keybind layout | DB `keymap_data.unlogin.*` | Custom key positions. The tuner flattens but doesn't remap. |
| `KeyMapInputType` | reg (`1`) | Keyboard input mode. |
| Mouse polling rate | hardware/mouse driver | 1000 Hz recommended. Check in mouse software. |
| Mouse DPI | hardware | Lower DPI + higher in-game sens = better precision. |
| `WheelScroll` bezier | reg `WheelScroll` | If wheel-aim feels laggy, these are the suspect (300 ms debounce). |
| Raw input | CODM / Windows | Bypasses mouse acceleration. Usually on by default. |

### 2.2 Visual / display

| Lever | Where | Notes |
|---|---|---|
| Render resolution | DB `local_user_config.landscape_render_wnd_size` | 916×515 in a 1280×976 box. Raising = sharper, more GPU cost. |
| `aspect_ratio` / `special_aspect_ratio` | DB `local_user_config` | Display geometry. |
| `auto_fullscreen` | DB host_config | Fullscreen on launch. |
| `rotation` | DB host_config | Screen rotation. |
| `DisableFullscreenOptimization` | reg `Settings\Common` (`0`) | Windows FSO. Try 1 if fullscreen feels laggy. |
| `ShowFps` | reg `Settings\GameLoop` (`0`) | In-emulator FPS counter. Flip to 1 while tuning. |
| `GameMode` | reg (`0`) | Windows Game Mode. |
| RenderMode / RenderUserType | reg `Settings\Common` | **Display only** — engine picks per map. Forcing can crash shaders. |
| `forceOpenGL` | deviceConfig.json (`false`) | Engine picks GL/Vulkan automatically. |
| `default_render_type` | androws.conf (`9`) | Engine render path. |
| GPU blacklist | renderer/*.json | RX 5700 XT already allowed (GL 4.6 + Vulkan). |
| Monitor refresh rate | Windows display settings | 144/240 Hz monitor → smoother. GameLoop caps at 120 fps though. |
| Brightness / gamma | CODM settings | Affects visibility in dark areas. |
| Color blind mode | CODM settings | If applicable. |
| HDR | Windows / monitor | Can add latency on some setups; test. |

### 2.3 Audio

| Lever | Where | Notes |
|---|---|---|
| `AndroidSystemVolume` | reg (`80`) | Emulator media volume. |
| Output/Input device GUIDs | reg (empty = default) | Pin a specific audio device. |
| `RecordAudioSwitch` | reg (`0`) | Mic into screen recordings. |
| In-game audio | CODM settings | Master/music/SFX/voice chat volumes. Footstep clarity matters for competitive. |
| 3D audio / spatial sound | CODM / Windows | Can improve directional awareness. |
| Windows audio enhancements | Sound settings | Disable "enhancements" if they add latency. |

### 2.4 Storage / I/O

| Lever | Where | Notes |
|---|---|---|
| D: free space | live: 86 GB | Engine wants ≥10 GB. Now fine. |
| `required_disk_space` | androws.conf (`10` GB) | Engine's own threshold. |
| `installedPkgAutoClearSwitch` | reg (`1`) | Clears xapk cache post-install. |
| `CustomizedPkgSavePath` | reg (empty) | Move GameLoopData off D: if needed. |
| VHD | `Image\data.vhd` (11.6 GB) | Grows with Android data; compact if large. |
| SSD vs HDD | hardware | GameLoop on SSD = faster load times, less I/O stutter. |
| Pagefile | Windows | Let Windows manage; ensure enough free space. |
| Defrag (HDD only) | Windows | Not needed on SSD. |

### 2.5 GameLoop service / autostart

| Lever | Where | Notes |
|---|---|---|
| `GameLoopService` start type | SCM (Automatic) | `sc config GameLoopService start=demand` stops auto-start. |
| Firewall rules | 10 inbound Allow | Normal set; two `*Acc` belong to accelerators. |
| `BossKey` | reg (`0`) | Panic-hide hotkey (Ctrl+`). Off by default. |
| `ExitPromptSwitch` / `BkgAppCloseBtnExit` | reg | Close button behavior. |
| `AppStoreCloseOption` | reg (`0`) | Minimize to tray. |
| `PreDownloadSwitch` / `AutoSilentUpdate` | reg (`1`/`1`) | Why builds update without asking. |
| `InstalledPkgAutoClearSwitch` | reg (`1`) | Clears xapk cache. |
| `ScreenRecordFilePath` | reg | Where built-in recorder saves. |

## Tier 3 — Niche / advanced

### 3.1 Emulator-level (deeper)

| Lever | Where | Notes |
|---|---|---|
| VT-x / Hyper-V | HKLM reg + BIOS | Required for speed. If VT flips to 0, CODM becomes a slideshow. |
| `VirtualEngineType` | reg (`28`) | Engine type. Don't change. |
| `HyperVState` | HKLM reg (`1`) | Hyper-V path active. |
| `VDIImageDiscType` | HKLM reg (`1`) | VHD backend. |
| `multipane` | androws.conf (`disabled`) | Multi-window mode. Disabled = single landscape. |
| `forcesoftvideo` | androws.conf (`true`) | CPU video decode. Leave ON. |
| `default_render_type` | androws.conf (`9`) | Engine render path. |
| `dpiPatch` | androws.conf (`true`) | DPI scaling patch. |
| `mock telephony/hardware` | androws.conf (`true`) | Emulator spoofing for anti-detection. Don't touch. |
| `named_filter` / `vattr_list` | androws.conf | Anti-detection paths. Don't touch. |
| `gpu_blacklist.json` | renderer/ | GPU compatibility. RX 5700 XT already allowed. |
| `fg_gpu_whitelist.json` | renderer/ | Frame-gen GPU whitelist. |
| `WheelScroll` bezier | reg | Scroll feel. If wheel-aim feels laggy, these are the suspect. |
| `LaunchDebounce` | reg | Anti-double-click guard for CODM launch. |
| `EnterKeyMapGuidanceTimes` | reg (`3`) | Keymap guidance counter. |

### 3.2 CODM in-game settings (not in tuner, user-facing)

| Category | Settings | Impact |
|---|---|---|
| Graphics | Quality (Low/Medium/High/Very High/Max), Frame rate (Low/Medium/High/Max/Ultra/Max), Graphics style (Realistic/Dynamic/Cel Shaded), Bloom, Ragdoll, Anti-aliasing, Shadows | Higher = better visuals, more GPU/CPU. |
| Sensitivity | Camera/ADS/Gyro per scope | Aim feel. |
| Controls | Auto-fire, Auto-sprint, Auto-pickup, Quick scope, Peek & Fire, Gyro | Gameplay feel. |
| Audio | Master/music/SFX/voice chat, 3D audio | Awareness. |
| Display | Brightness, color blind mode, HUD scale, minimap | Visibility. |
| Network | Server region (auto-select), low-latency mode | Ping/stability. |
| Accessibility | Various | As needed. |

### 3.3 Hardware (the real ceiling)

| Component | Impact on GameLoop/CODM |
|---|---|
| CPU (cores/speed) | VM cores slice. More cores = smoother emulation. 8c/16GB host is good. |
| RAM | VM RAM slice. 16 GB host = 10 GB VM comfortable. |
| GPU | Render. RX 5700 XT (8.5 GB VRAM) handles clarity 3 fine. |
| SSD | Load times, VHD I/O. HDD = stutter. |
| Monitor | Refresh rate. GameLoop caps at 120 fps regardless. |
| Mouse | Polling rate (1000 Hz), DPI, sensor quality. |
| Network | Bandwidth + latency to server. Algeria → EU = 30–50 ms floor. |

### 3.4 Anti-cheat / account safety

| Rule | Why |
|---|---|
| Don't modify `named_filter` / `vattr_list` | Breaks anti-detection; can trigger bans. |
| Don't touch `sdid` / `vdid` / `cert` / `oaid` | Device/account fingerprint. |
| Don't share `SyzsGuid` / `seed_id` | Account identifiers. |
| Don't use aimbots/macros | Ban risk. |
| Don't edit `offcial_keymap_data2.*` rows | Trigger-guarded; tuner only writes user blobs. |
| Don't share diagnostic reports publicly | They contain device identifiers (scrubbed by tuner, but still). |

## What's NOT worth it (placebo / risky)

| Thing | Why skip |
|---|---|
| QoS packet scheduler | Marginal (±1–3 ms), often placebo. |
| Game Mode (Windows) | Can starve the emulator on weak CPUs. |
| Fullscreen optimization toggle | Mixed results; test if curious. |
| NIC interrupt moderation / LSO | Niche; usually no measurable difference. |
| `RenderMode` manual override | Engine-managed per map; forcing can crash shaders. |
| `softvideo` off | Breaks video (lobby/killcam). |
| `forceOpenGL` on | Engine picks better path automatically. |
| `multipane` enable | Not useful for CODM (single landscape). |
| Registry `BossKey` | Niche panic-hide feature. |
| `ShowFps` | Useful for tuning, not for gameplay. |
| `GameMode` reg | Windows Game Mode; mixed results. |

## Suggested tuner feature roadmap

Based on this catalog, the highest-value additions to the tuner (beyond what's done):

| Priority | Feature | Rationale |
|---|---|---|
| P0 (done) | Keymap auto-switch fix | Core value proposition. |
| P0 (done) | VM cores/RAM, clarity, low-latency, frame-gen | Performance page. |
| P0 (done) | Process priorities | VM & Priority page. |
| P0 (done) | Backups, logs, drift detection, diagnostic report | Safety/visibility. |
| P1 (done) | Network & Servers tab | Visibility into connections, game server, high-ping help. |
| P1 | DNS override toggle (guest) | 114 is dead from Algeria; let users switch resolvers in-UI. |
| P1 | Wi-Fi → wired prompt | Detect Wi-Fi and suggest Ethernet. |
| P2 | In-game settings reference | Show recommended CODM graphics/sensitivity settings. |
| P2 | Bufferbloat test | One-click test + SQM guidance. |
| P2 | Power plan toggle | High Performance / Balanced switch. |
| P2 | `ShowFps` toggle | In-emulator FPS counter. |
| P3 | `BossKey` toggle | Panic-hide hotkey. |
| P3 | WheelScroll tuning | If wheel-aim feels laggy. |
| P3 | Storage cleanup helper | Free D: space (old ISOs, LDPlayer, duplicates). |

## What's already monitorable (for the "feel in control" goal)

- Dashboard: 6 live cards (processes, VM alloc, active keymap, CODM settings, drive headroom, service).
- VM & Priority: running process table (PID + memory).
- Network & Servers: guest connections, game server + ping, high-ping help.
- Backups: timestamped DB + smk.conf snapshots.
- Logs: everything that happened, secrets stripped, 500-line ring.
- About: diagnostic report (200 KB cap, scrubbed).
- Drift: `⚠ Drifted` badge + per-key "changed: was X → now Y".
- Monitor-Jitter.ps1 / Monitor-Connection.ps1: real-time ping/jitter/loss CSV.

---

*Sources: docs 01–11, native C++ source, live system probes 2026-10-18, CODM in-game settings.*