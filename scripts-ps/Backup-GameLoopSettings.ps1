#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Read-only backup of GameLoop settings (registry). Zero changes to GameLoop.

.DESCRIPTION
  Exports HKCU\Software\Tencent\GameLoop to a timestamped .reg file (for
  Restore-GameLoopSettings.ps1) plus a human-readable .txt snapshot of the
  two keys this project touches:
    Settings\Common  -> RenderMode, RenderUserType (+ alloc shown, never written)
    Settings\GameLoop -> ShowFps (+ neighbours shown, never written)
  Safe to run anytime, GameLoop running or not. Creates nothing outside backups/.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File Backup-GameLoopSettings.ps1
  powershell -ExecutionPolicy Bypass -File Backup-GameLoopSettings.ps1 -BackupDir D:\temp\gl-bak

.REVERT
  N/A (this script changes nothing). To undo someone else's change, run
  Restore-GameLoopSettings.ps1 -BackupFile <this script's .reg output>.
#>
[CmdletBinding()]
param(
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

$branch      = 'HKCU\Software\Tencent\GameLoop'
$psBranch    = 'HKCU:\Software\Tencent\GameLoop'
$psCommon    = "$psBranch\Settings\Common"
$psLauncher  = "$psBranch\Settings\GameLoop"

if (-not (Test-Path -LiteralPath $psBranch)) {
  Fail "Not found: $psBranch -- is GameLoop installed for this user?" 2
}

New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null
$stamp   = Get-Date -Format 'yyyyMMdd-HHmmss'
$regFile = Join-Path $BackupDir "GameLoop-$stamp.reg"
$txtFile = Join-Path $BackupDir "GameLoop-$stamp.txt"

# 1. Machine-readable full-branch export (what Restore imports).
# Array args: handles spaces in paths with no quote gymnastics.
& reg.exe @('export', $branch, $regFile, '/y') | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $regFile)) {
  Fail 'reg.exe export failed.' 1
}

# 2. Human-readable snapshot of exactly what we care about.
$common   = Get-ItemProperty -Path $psCommon -ErrorAction SilentlyContinue
$launcher = Get-ItemProperty -Path $psLauncher -ErrorAction SilentlyContinue
@(
  "GameLoop settings snapshot $stamp (READ-ONLY, no changes made)"
  ''
  '[Settings\Common]'
  ("  RenderMode                    = {0}  (0=Auto 1=OpenGL+ 2=DirectX+ 3=Vulkan)" -f $common.RenderMode)
  ("  RenderUserType                = {0}  (0=auto 1=manually overridden)" -f $common.RenderUserType)
  ("  PerformanceMode               = {0}  (shown only, never written by these scripts)" -f $common.PerformanceMode)
  ("  PerformanceCpuCoreNum         = {0}  (shown only, never written by these scripts)" -f $common.PerformanceCpuCoreNum)
  ("  PerformanceMemoryGb           = {0}  (shown only, never written by these scripts)" -f $common.PerformanceMemoryGb)
  ("  DisableFullscreenOptimization = {0}  (shown only, never written by these scripts)" -f $common.DisableFullscreenOptimization)
  ''
  '[Settings\GameLoop]'
  ("  ShowFps              = {0}" -f $launcher.ShowFps)
  ("  GameMode             = {0}  (shown only)" -f $launcher.GameMode)
  ("  AndroidSystemVolume  = {0}  (shown only)" -f $launcher.AndroidSystemVolume)
  ("  ScreenRecordFilePath = {0}  (shown only)" -f $launcher.ScreenRecordFilePath)
) | Set-Content -LiteralPath $txtFile -Encoding UTF8

Write-Output "Backup OK (no changes made):"
Write-Output "  $regFile"
Write-Output "  $txtFile"
