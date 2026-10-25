# 06 — Registry settings: what GameLoop stores + what it's for (your live values)

All dumped live 2026-09-20. Paths are `HKCU\Software\Tencent\...` unless noted.

## HKLM\SOFTWARE\WOW6432Node\Tencent\GameLoop (machine-wide, 3 values)

| Value | Yours | Meaning |
|---|---|---|
| `VDIImageDiscType` | 1 | which VHD backend the VM image uses |
| `HyperVState` | 1 | Hyper-V path active (matches `fix_hyperv.bat` in resources) |
| `VtState` | 1 | CPU VT-x enabled in BIOS — required for the emulator to run at speed |

If VT ever flips to 0, CODM turns into a slideshow — that's the first thing to check.

## HKCU\...\GameLoop (root)

- `Version` = 7.0.19.05 (uninstall entry), `ClientType` 0, `CanSkipClientType` 0, `SupportMMIO` True
- `com.activision.callofduty.shooter_toolbar_item_usage` = your toolbar clicks (type 220 x4, type 212 x2 with timestamps) — usage telemetry for which emulator buttons you actually press in CODM
- `ipc_*_analysis*` = internal process-message stats (from→to msg_id, success/total, avg_cost ms). Pure diagnostics, safe to ignore.

## Settings\Common — YOUR performance settings (the important ones)

| Value | Yours | Meaning |
|---|---|---|
| `PerformanceMode` | 3 | preset tier (your tier defs live in `GameLoop\SimulatorHardwareConfig`, see below) |
| `PerformanceMemoryGb` | 10 | RAM given to the VM |
| `PerformanceCpuCoreNum` | 6 | CPU cores given to the VM (host has 8 — snapshot in `MachineStates` confirms `cpu_core_count: 8`) |
| `RenderMode` | 0 | renderer select (0 = auto; engine can use OpenGL/Vulkan — your GPU probes below say Vulkan supported, GL 4.6) |
| `RenderUserType` | 0 | 0 = you never overrode renderer manually |
| `DisableFullscreenOptimization` | 0 | Windows fullscreen-optimizations left on |
| `OutputDeviceGuid` / `InputDeviceGuid` | empty | default audio devices (your `AndroidSystemVolume` 80 below is the emulator-side volume) |

## Settings\GameLoop — launcher / store / CODM behaviour

| Value | Yours | For |
|---|---|---|
| `ScreenRecordFilePath` | `C:\Users\SKULL\Videos\GameLoop` | where built-in screen recorder saves (check this folder — it read empty today) |
| `AndroidSystemVolume` | 80 | emulator media volume |
| `InstalledPkgAutoClearSwitch` | 1 | auto-delete cached xapk after install (saves D: space) |
| `ExitPromptSwitch` / `BkgAppCloseBtnExit` | 1 / 0 | close button shows prompt, doesn't hard-kill background app |
| `GameMode` / `ShowFps` | 0 / 0 | game-mode + FPS overlay both off — turn ShowFps to 1 if you want the in-emulator counter |
| `KeyMapInputType` | 1 | keyboard input mode for keymaps |
| `EnterKeyMapGuidanceTimes` | 3 | you've opened keymap guidance 3x |
| `AndroidAdbMode` | 0 | ADB restricted mode (port 5555 still listens while emulator runs — see 01) |
| `RecordAudioSwitch` | 0 | screen-record mic off |
| `EnableOpenVfsOnMarketStart` | 0 | don't auto-open shared-folder view |
| `CustomizedPkgSavePath` / `CustomizedPcAppOneclickInstallPath` | empty | default paths (i.e. under GameLoopData) |
| `GlLoginLaunchServiceCounter3` | 7.0.159.0,… | launch counter blob |

## Settings\GameLoopSettings — store behaviour

`PreDownloadSwitch` 1 (pre-fetch updates), `AutoSilentUpdate` 1 (store self-updates — why builds move without asking), `CreateAppShortcut` 1, `NotificationOption` 1 with empty blocklist, `AppStoreCloseOption` 0 (close minimizes to tray — matches `is_system_tray_mode` 1 below), `BossKey` 1 + `BossKeyHideTaskbarTray` 1 + `AndrowsBossKey` Ctrl+` (modifiers 2, vk 192) = panic-hide hotkey, `DefaultReplaceLocalApk` 1, `EtwApkTipsMode/FileTipsMode/AccTipsSwitch` 1.

## GameLoop\GameLoop — install + hardware snapshot

- `InstallTime` 2026/09/19 13:47:30, `Version` 7.0.159.0, `Image` (next section) 5.10.7000.123, `VirtualEngineType` 28, `SyzsGuid`/`svid` = your install ID
- `SimulatorHardwareConfig`: low 2c/3GB, mid 4c/4GB, high 4c/6GB, recommend 4c/6GB, max 7c/12GB; `DevicePerfTier` 1 → you're on a mid-tier auto pick, but your Common override (6c/10GB) sits above it
- `MachineStates` snapshot: 8 CPU cores, 5% load, 39GB total / 19.6GB avail memory (39GB?? — includes pagefile-backed; phys 16.1GB total / 9.3GB avail), `CurrentDpi` 1.00, `StoreWndInScreenIndex` 0, `RunningAppInfo` count 0 at sample time, rough IP geo lat/long stored (28.02/1.65 — coarse, from IP lookup)
- `AppStoreLaunchTime`, `ResHubSchedTime` = scheduler timestamps

## GameLoop\Image — engine version: 5.10.7000.123

## GameLoop\Mark — one-shot flags (all 1 = already done, won't re-ask)

`bugfix.switch.autostart.mark`, `pcyyb.sdk.desktop_link.mark`, `pcyyb.sdk.break.trayhidden.mark`, `ReplenishAppSource`, `prepare_game_mode`, `full_screen_toast_mark_com.activision.callofduty.shooter` (the "press F11 for fullscreen" toast won't nag again for CODM).

## GameLoop\Duration — maintenance timestamps (unix-ms)

`GarbageClean`, `HyperVReport`, `QimeiChangeCheck`, `StuckLogUploadTime`, `ReportAppAnrTimeStamp`, `dyn_profile/st_profile`, `vulkan_version_report`, `id_mapping_report`, IPC resets… — the emulator's "last did X at" ledger. If cleanup or telemetry ever looks stuck, compare these across days.

## GameLoop\Env — machine + identity + run state

- `installer_locale` en; `vulkan_info` AMD Radeon RX 5700 XT, apiVersion 13, driver 32.0.12011.1036, `vulkan_support` 1
- `VPoDioa/VOPoDioa/VWiDioa` = encrypted device blobs; `ams_union_id_v2` = login/session union ID; `cert_path` → `%APPDATA%\Tencent\GameLoop\config\7f18….dat` + `cert_hash` FCB55… — device cert, don't delete (forces re-login/re-attestation)
- `ImageBootCount` 9, `VmRunningStatus` 2, `VmCrashStatus` 0, `AppRunningStatus` 2, `GameStarted` 1, `KeepLaunchVmSilent` 1
- `Env\Values\LastLnk` = GameLoop
- `Env\GraphicInfo\dxdiag.exe`: 1 GPU, 8.5GB dedicated VRAM, GL driver 32.0.12011.1036 (2024-09-26); `…\GameLoopAssistant.exe`: GL vendor ATI, renderer RX 5700 XT, version 4.6.0

## Settings\GameLoop\WheelScroll — mouse/scroll feel (all live)

`UseNewScroll` 1, `UseBezierGesture` 1, bezier debounce discard 300ms / merge 50ms, touch offsets 15px, release timeouts 400/100/100/80ms, wheel min interval 16ms (~60Hz), fast-mode ≥30 inputs / ≥80Hz, velocity thresholds 100/200, swipe 1000%/scale 100/10 steps/120ms, touchpad pixels-per-notch 600, `UseNewTouchpadMode` 0, `UseConfigFromRegistry` 0. Translation: scroll input for CODM (map zoom, wheel-weapon) is bezier-smoothed with anti-jitter — if aiming with wheel feels laggy, this key is why.

## GameLoop\LaunchDebounce — `com.activision.callofduty.shooter` = 17026765

Anti-double-click guard for launching CODM (ms-scale token). Not a setting to edit.

## Androws + SDK_CloudGame

`Androws\UninstallConfig`: `EnableUploadLogOnUninstall` 0 (won't upload logs if you uninstall), wait 10s. `SDK_CloudGame` empty. `Androws` root itself has no values — config lives in Configs.db + GameLoopData jsons (see 07).

## OS-level leftovers checked

- IFEO (`Image File Execution Options`): NO entries for GameLoop/AppMarket/AndroidEmulator/aow_exe — your `D:\stuff\gameloop_priority.reg` was never applied (or was cleaned). So no forced High priority exists at OS level right now.
- Firewall: 10x inbound Allow — Store, Main, Assistant, Assistant Toast, Download Service, Store Render, Service, AboxHeadless, XyAcc, LeigodAcc. Normal set; the two `*Acc` (accelerator) rules belong to network-boost components.
