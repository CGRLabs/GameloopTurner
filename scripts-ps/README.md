# GameLoop render scripts (admin PowerShell, reversible)

Local-only helpers. They do **not** touch the Tuner app in `../src/`.
Every change is **backup-first + verified + revertible**.

## Files

| File | Does | Writes? |
|---|---|---|
| `Backup-GameLoopSettings.ps1` | Exports `HKCU\Software\Tencent\GameLoop` to `backups\GameLoop-<stamp>.reg` + readable `.txt` | No registry writes |
| `Set-GameLoopRender.ps1` | Sets `RenderMode` / `RenderUserType` / `ShowFps` only, with backup + re-read verify | Yes, 3 values max |
| `Restore-GameLoopSettings.ps1` | Restores a `.reg` (backs up current first, so restore is itself reversible) | Yes, via `reg.exe import` |
| `backups/` | Timestamped `.reg` + `.txt` pairs | — |

## Values touched (and ONLY these)

- `Settings\Common\RenderMode` DWORD: `0`=Auto, `1`=OpenGL+, `2`=DirectX+, `3`=Vulkan
- `Settings\Common\RenderUserType` DWORD: `0`=auto, `1`=manually overridden (auto-derived from RenderMode)
- `Settings\GameLoop\ShowFps` DWORD: `0`=off, `1`=in-emulator FPS counter

Explicitly **never** touched: cores/RAM (`PerformanceCpuCoreNum/MemoryGb`),
`PerformanceMode`, `DisableFullscreenOptimization`, any `Configs.db` keymap or
perf flag, services, firewall, DNS, priorities.

## Live snapshot 2026-09-25 (read-only probe, this PC)

- `RenderMode 0` (Auto), `RenderUserType 0`, `ShowFps 0`, `GameMode 0`
- `PerformanceMode 3`, `CpuCoreNum 7`, `MemoryGb 13` on 8c / 15.7GB host
  → 13GB leaves ~2.7GB for Windows: aggressive, likely micro-freeze source.
  These scripts do **not** change it; flagged for a later, separate step.
- Running at probe: `GameLoop, Assistant, DldSvr, Service` (Emulator/VM idle)

## Usage (elevated PowerShell)

```powershell
# 1. Backup (safe anytime):
powershell -ExecutionPolicy Bypass -File Backup-GameLoopSettings.ps1

# 2. Preview a change (writes NOTHING):
powershell -ExecutionPolicy Bypass -File Set-GameLoopRender.ps1 -RenderMode 3 -ShowFps 1 -DryRun

# 3. Apply (close GameLoop tray -> Quit first):
powershell -ExecutionPolicy Bypass -File Set-GameLoopRender.ps1 -RenderMode 3 -ShowFps 1

# 4. Revert (newest backup; pre-restore backup taken automatically):
powershell -ExecutionPolicy Bypass -File Restore-GameLoopSettings.ps1
powershell -ExecutionPolicy Bypass -File Restore-GameLoopSettings.ps1 -ListBackups
```

Suggested A/B order for tearing: `3 Vulkan` → test → `2 DirectX+` → test →
`0 Auto` if neither wins. One variable at a time, one match each, `ShowFps 1`
on while comparing.

## Exit codes (all scripts)

## Exit codes (all scripts)

`0` = ok (Set also re-read + verified) · `1` = failed / verify mismatch ·
`2` = bad usage / missing GameLoop / no backups · `3` = GameLoop running
(re-run after tray → Quit, or with `-AllowRunning`).

## Safety rules baked in

1. `#Requires -RunAsAdministrator` on all three.
2. `Set` refuses when any `GameLoop*` process runs unless `-AllowRunning`
   (running app can overwrite registry edits). `GameLoopService` is excluded —
   it's Automatic and always alive; only UI/Emulator/VM/helpers matter.
3. `Set` backs up before writing (refuses to write if backup fails).
4. `Set` re-reads AFTER and exits 1 on mismatch.
5. `Restore` backs up current BEFORE importing, so a bad restore is undoable.
6. `-DryRun` performs zero registry writes and zero backup files.

## If something looks wrong

1. `Restore-GameLoopSettings.ps1` (newest backup) → tray Quit → relaunch.
2. Still wrong → restore the `GameLoop-<stamp>.reg` created *by the restore*
   itself (the pre-restore copy) → relaunch.
3. Backend black-screen? Same path: Auto (`RenderMode 0`) is the known-good
   fallback; updates can also reset it to 0 on their own.
