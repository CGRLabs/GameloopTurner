#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Set GameLoop renderer backend + FPS overlay (backup-first, verified, reversible).

.DESCRIPTION
  Writes ONLY these values, nothing else:
    HKCU\Software\Tencent\GameLoop\Settings\Common\RenderMode      (DWORD 0-3)
    HKCU\Software\Tencent\GameLoop\Settings\Common\RenderUserType  (DWORD 0/1, auto-derived)
    HKCU\Software\Tencent\GameLoop\Settings\GameLoop\ShowFps       (DWORD 0/1)
  RenderMode labels (match tuner src/core/registry.py):
    0 = Auto (engine picks per game) | 1 = OpenGL+ | 2 = DirectX+ | 3 = Vulkan
  RenderUserType is derived: 0 when RenderMode=0 (never overridden), 1 otherwise
  (user overrode renderer). Override with -RenderUserType if you know why.
  Flow: read BEFORE -> backup (.reg) -> write -> re-read AFTER + verify.
  -DryRun previews with ZERO registry writes and ZERO backup files.

  RISKS (read before running):
  - Forcing a backend can black-screen / stutter if the GPU/driver dislikes it.
    Your RX 5700 XT probes OK for GL 4.6 + Vulkan, but Auto exists for a reason.
  - Engine updates can reset RenderMode to 0. Re-run backup+set after updates.
  - Registry edits while GameLoop UI runs can be overwritten by the running app,
    so the script REFUSES when any GameLoop* process runs unless -AllowRunning.

.EXAMPLE
  # Preview only, safe anytime (writes nothing):
  powershell -ExecutionPolicy Bypass -File Set-GameLoopRender.ps1 -RenderMode 3 -ShowFps 1 -DryRun

.EXAMPLE
  # Force Vulkan + FPS overlay (close GameLoop tray -> Quit first):
  powershell -ExecutionPolicy Bypass -File Set-GameLoopRender.ps1 -RenderMode 2 -ShowFps 1

.EXAMPLE
  # Back to engine Auto + overlay off:
  powershell -ExecutionPolicy Bypass -File Set-GameLoopRender.ps1 -RenderMode 0 -ShowFps 0

.REVERT
  powershell -ExecutionPolicy Bypass -File Restore-GameLoopSettings.ps1
  (restores newest backups\GameLoop-*.reg, which this script creates every run)
#>
[CmdletBinding()]
param(
  [int]$RenderMode = -1,      # -1 = leave unchanged; else 0..3
  [int]$ShowFps = -1,         # -1 = leave unchanged; else 0/1
  [int]$RenderUserType = -1,  # -1 = auto-derive; else 0/1 (advanced)
  [switch]$AllowRunning,      # required if any GameLoop* process is alive
  [switch]$DryRun,            # preview only: no writes, no backup files
  [switch]$NoBackup,          # NOT recommended: skip the safety backup
  # Empty = <script-dir>\backups (resolved in body: $PSScriptRoot is not
  # available in param defaults of advanced scripts under powershell.exe -File).
  [string]$BackupDir = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Write-Error becomes terminating under 'Stop' and would swallow our exit
# codes, so guards use Fail (stderr + explicit code) instead.
function Fail([string]$Message, [int]$Code) {
  [Console]::Error.WriteLine($Message)
  exit $Code
}

if (-not $BackupDir) {
  $here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
  $BackupDir = Join-Path $here 'backups'
}

$psCommon   = 'HKCU:\Software\Tencent\GameLoop\Settings\Common'
$psLauncher = 'HKCU:\Software\Tencent\GameLoop\Settings\GameLoop'
$labels = @{ 0 = 'Auto (engine picks per game)'; 1 = 'OpenGL+'; 2 = 'DirectX+'; 3 = 'Vulkan' }

if ($RenderMode -eq -1 -and $ShowFps -eq -1) {
  Fail 'Nothing to do: pass -RenderMode 0..3 and/or -ShowFps 0/1. Use -DryRun to preview.' 2
}
if ($RenderMode -ne -1 -and ($RenderMode -lt 0 -or $RenderMode -gt 3)) {
  Fail 'RenderMode must be 0, 1, 2 or 3.' 2
}
if ($ShowFps -ne -1 -and ($ShowFps -ne 0 -and $ShowFps -ne 1)) {
  Fail 'ShowFps must be 0 or 1.' 2
}
if ($RenderUserType -ne -1 -and ($RenderUserType -ne 0 -and $RenderUserType -ne 1)) {
  Fail 'RenderUserType must be 0 or 1.' 2
}
foreach ($p in @($psCommon, $psLauncher)) {
  if (-not (Test-Path -LiteralPath $p)) { Fail "Missing key: $p" 2 }
}

# BEFORE (read-only).
$beforeCommon   = Get-ItemProperty -Path $psCommon
$beforeLauncher = Get-ItemProperty -Path $psLauncher
$wantMode = if ($RenderMode -eq -1) { [int]$beforeCommon.RenderMode } else { $RenderMode }
$wantType = if ($RenderUserType -ne -1) { $RenderUserType } elseif ($RenderMode -eq -1) { [int]$beforeCommon.RenderUserType } elseif ($wantMode -eq 0) { 0 } else { 1 }
$wantFps  = if ($ShowFps -eq -1) { [int]$beforeLauncher.ShowFps } else { $ShowFps }

Write-Output ("BEFORE: RenderMode={0} ({1}) RenderUserType={2} ShowFps={3}" -f `
  $beforeCommon.RenderMode, $labels[[int]$beforeCommon.RenderMode], $beforeCommon.RenderUserType, $beforeLauncher.ShowFps)
Write-Output ("WANT:   RenderMode={0} ({1}) RenderUserType={2} ShowFps={3}" -f `
  $wantMode, $labels[$wantMode], $wantType, $wantFps)

if ($DryRun) {
  Write-Output 'DRYRUN: zero registry writes, zero backup files. Re-run without -DryRun to apply.'
  exit 0
}

# Guard: running app can clobber registry edits. GameLoopService is excluded
# (Automatic service, always alive; matches tuner core/procs.py running()).
$live = Get-Process GameLoop* -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -ne 'GameLoopService' } | Select-Object -ExpandProperty ProcessName -Unique
if ($live -and -not $AllowRunning) {
  Fail ("GameLoop is running ({0}). Tray -> Quit first, or re-run with -AllowRunning (edit may be overwritten)." -f ($live -join ', ')) 3
}
if ($live) { Write-Warning ("Proceeding with GameLoop alive ({0}) because -AllowRunning was given." -f ($live -join ', ')) }

# Backup-first (unless explicitly skipped).
if (-not $NoBackup) {
  $backupScript = Join-Path $PSScriptRoot 'Backup-GameLoopSettings.ps1'
  & powershell -NoProfile -ExecutionPolicy Bypass -File $backupScript -BackupDir $BackupDir
  if ($LASTEXITCODE -ne 0) { Fail 'Backup failed -- refusing to write.' 1 }
} else {
  Write-Warning 'Running with -NoBackup: no .reg safety net for this run.'
}

# Write (only the 3 named values).
if ($RenderMode -ne -1) {
  Set-ItemProperty -Path $psCommon -Name 'RenderMode' -Value $wantMode -Type DWord
  Set-ItemProperty -Path $psCommon -Name 'RenderUserType' -Value $wantType -Type DWord
}
if ($ShowFps -ne -1) {
  Set-ItemProperty -Path $psLauncher -Name 'ShowFps' -Value $wantFps -Type DWord
}

# AFTER + verify (re-read proves the write stuck).
$afterCommon   = Get-ItemProperty -Path $psCommon
$afterLauncher = Get-ItemProperty -Path $psLauncher
Write-Output ("AFTER:  RenderMode={0} ({1}) RenderUserType={2} ShowFps={3}" -f `
  $afterCommon.RenderMode, $labels[[int]$afterCommon.RenderMode], $afterCommon.RenderUserType, $afterLauncher.ShowFps)

$ok = ([int]$afterCommon.RenderMode -eq $wantMode) -and ([int]$afterCommon.RenderUserType -eq $wantType) -and ([int]$afterLauncher.ShowFps -eq $wantFps)
if (-not $ok) {
  Fail 'VERIFY FAILED: re-read does not match WANT. Restore newest backup and report.' 1
}
Write-Output 'OK + verified. Restart GameLoop (tray -> Quit, relaunch) for the backend to take effect.'
Write-Output 'Revert: Restore-GameLoopSettings.ps1 (newest backups\GameLoop-*.reg).'
