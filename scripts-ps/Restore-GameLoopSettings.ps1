#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Restore GameLoop settings from a Backup-GameLoopSettings.ps1 .reg file.

.DESCRIPTION
  Safety-first revert: backs up the CURRENT state first (pre-restore .reg),
  then imports the chosen backup via reg.exe, then re-reads and shows AFTER.
  By default restores the NEWEST backups\GameLoop-*.reg. Pass -BackupFile for
  a specific one. Restart GameLoop (tray -> Quit, relaunch) afterwards.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File Restore-GameLoopSettings.ps1 -ListBackups
  powershell -ExecutionPolicy Bypass -File Restore-GameLoopSettings.ps1
  powershell -ExecutionPolicy Bypass -File Restore-GameLoopSettings.ps1 -BackupFile .\backups\GameLoop-20260925-120000.reg
#>
[CmdletBinding()]
param(
  [string]$BackupFile = '',
  [switch]$ListBackups,
  [switch]$AllowRunning,
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

$files = @(Get-ChildItem -Path $BackupDir -Filter 'GameLoop-*.reg' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending)
if ($ListBackups) {
  if (-not $files) { Write-Output "No backups in $BackupDir yet."; exit 0 }
  $files | Format-Table LastWriteTime, Name, Length -AutoSize | Out-String -Width 200 | Write-Output
  exit 0
}
if ($BackupFile) {
  $target = Get-Item -LiteralPath $BackupFile -ErrorAction Stop
} else {
  if (-not $files) { Fail "No backups in $BackupDir. Nothing to restore." 2 }
  $target = $files[0]
  Write-Output ("Using newest backup: {0}" -f $target.FullName)
}

# GameLoopService excluded: Automatic service, always alive (same as Set + tuner).
$live = Get-Process GameLoop* -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -ne 'GameLoopService' } | Select-Object -ExpandProperty ProcessName -Unique
if ($live -and -not $AllowRunning) {
  Fail ("GameLoop is running ({0}). Tray -> Quit first, or re-run with -AllowRunning." -f ($live -join ', ')) 3
}

# Pre-restore backup of current (so the restore itself is reversible).
$backupScript = Join-Path $PSScriptRoot 'Backup-GameLoopSettings.ps1'
& powershell -NoProfile -ExecutionPolicy Bypass -File $backupScript -BackupDir $BackupDir
if ($LASTEXITCODE -ne 0) { Fail 'Pre-restore backup failed -- refusing to restore.' 1 }

& reg.exe @('import', $target.FullName) | Out-Null
if ($LASTEXITCODE -ne 0) { Fail ("reg.exe import failed for {0}" -f $target.FullName) 1 }

$afterCommon   = Get-ItemProperty -Path $psCommon -ErrorAction SilentlyContinue
$afterLauncher = Get-ItemProperty -Path $psLauncher -ErrorAction SilentlyContinue
Write-Output ("Restored from: {0}" -f $target.FullName)
Write-Output ("AFTER: RenderMode={0} RenderUserType={1} ShowFps={2}" -f `
  $afterCommon.RenderMode, $afterCommon.RenderUserType, $afterLauncher.ShowFps)
Write-Output 'Restart GameLoop (tray -> Quit, relaunch). If still wrong, restore the pre-restore backup this script just created.'
