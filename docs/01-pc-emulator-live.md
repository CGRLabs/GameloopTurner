# 01 — GameLoop emulator LIVE on this PC

Checked 2026-09-20 via Get-Process / Get-Service / Win32_Process / Get-NetTCPConnection / registry / uninstall keys.

## Status: RUNNING (6 processes)

| Process | PID (at check) | Note |
|---|---|---|
| GameLoop | 1512 | main UI/launcher, started 2026-09-19 18:11, ~79MB WS |
| GameLoopAssistant | 15592 | helper, 15.6MB |
| GameLoopDldSvr | 13980 | download server, listens 127.0.0.1:58320 + [::1]:58320 |
| GameLoopEmulator | 8640 | emulator frontend, started 2026-09-20 11:44, listens 0.0.0.0:15120 + 0.0.0.0:5555 (ADB) |
| GameLoopService | 17200 | service host, listens 127.0.0.1:5284 |
| GameLoopVm | 20412 | VM backend, started 2026-09-20 11:44 |

Old process names (`AppMarket.exe`, `AndroidEmulator.exe`, `aow_exe.exe`) NOT running — your `D:\stuff` priority scripts target those old names, so they are stale (see 03).

## Service

- `GameLoopService` — Status: Running, StartType: Automatic
- Means it starts on boot even if you don't open GameLoop.

## Listening ports (at check)

| Port | Process | Meaning |
|---|---|---|
| 127.0.0.1:58320 + [::1]:58320 | GameLoopDldSvr (13980) | local download/update server |
| 127.0.0.1:25120, 127.0.0.1:13900, 127.0.0.1:5283 | GameLoop (1512) | UI IPC |
| 0.0.0.0:15120 | GameLoopEmulator (8640) | emulator channel, LAN-reachable |
| 0.0.0.0:5555 | GameLoopEmulator (8640) | ADB — Android Debug Bridge, LAN-reachable |
| 127.0.0.1:5284 | GameLoopService (17200) | service IPC |

Note: `Win32_Process CommandLine/ExecutablePath` returned empty for these (protected/PPL or WMI blind spot) — PIDs + ports above are the reliable IDs.

No scheduled task matched `GameLoop|Tencent|TxGame` (schtasks query empty). No Win32_StartupCommand match either — autostart is via the Automatic service, not Startup folder.

## Registry

- `HKLM\SOFTWARE\WOW6432Node\Tencent\GameLoop`: `VDIImageDiscType=1, HyperVState=1, VtState=1`
- `HKCU\Software\Tencent`: subkeys `Androws`, `GameLoop`, `SDK_CloudGame`
- Uninstall: `HKLM\...\Uninstall` → `GameLoop, 7.0.19.05, Hong Kong Gathering Media Limited` (no InstallLocation value stored)
- App build on disk is `7.0.159.0` under `D:\Program Files\Tencent\GameLoop\Application\` (launcher + uninstaller version can differ from the 7.0.19.05 ARP entry — normal).

## Install location (NOT C:\Program Files)

- Real: `D:\Program Files\Tencent\` (data-drive copy, matches old drive-audit note)
- `C:\Program Files\TxGameAssistant`, `C:\Program Files (x86)\TxGameAssistant`, `%LOCALAPPDATA%\Tencent` → all MISSING
- `%APPDATA%\Tencent\` EXISTS: `beacon/, BeaconConfig/, DeskUpdate/, DtConfig/, GameLoop/, qimei/`
