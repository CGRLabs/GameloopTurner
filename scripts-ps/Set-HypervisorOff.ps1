#Requires -RunAsAdministrator
<#
.SYNOPSIS
  VT-x experiment APPLY: stops Windows hypervisor at boot (bcdedit
  hypervisorlaunchtype off) so VirtualBox can use VT-x directly.
  Records BEFORE state to backups/hv-before-<stamp>.txt first.

.DESCRIPTION
  Writes ONE bcdedit value. Takes effect after reboot. Expected side effects
  until reverted: Memory Integrity shows OFF, GameLoop/WSL2/Sandbox may fail
  to boot (they want the hypervisor). Revert with Set-HypervisorAuto.ps1 or
  System Restore (see New-VTxRestorePoint.ps1).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File Set-HypervisorOff.ps1
  # then: reboot when ready, check VBox.log for "HM: Using VT-x"
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Fail([string]$Message, [int]$Code) {
  [Console]::Error.WriteLine($Message)
  exit $Code
}

# bcdedit.exe lives in real System32; a 32-bit shell gets redirected to
# SysWOW64 where it does not exist. Resolve explicitly.
$bcd = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
if (-not (Test-Path -LiteralPath $bcd)) {
  $bcd = Join-Path $env:SystemRoot 'Sysnative\bcdedit.exe'
}
if (-not (Test-Path -LiteralPath $bcd)) { Fail 'bcdedit.exe not found.' 1 }

$here = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$bakDir = Join-Path $here 'backups'
New-Item -ItemType Directory -Path $bakDir -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$rec = Join-Path $bakDir ("hv-before-{0}.txt" -f $stamp)

$before = & $bcd /enum active
if ($LASTEXITCODE -ne 0) { Fail 'bcdedit read failed.' 1 }
$mb = $before | Select-String 'hypervisorlaunchtype' | Select-Object -First 1
$cur = if ($mb) { $mb.Line.Trim() } else { '(absent = default auto)' }
Write-Output ('BEFORE: {0}' -f $cur)
@('BEFORE ' + $stamp, '') + $before | Set-Content -LiteralPath $rec -Encoding UTF8
Write-Output ('recorded: {0}' -f $rec)

& $bcd /set "{current}" hypervisorlaunchtype off | Out-Null
if ($LASTEXITCODE -ne 0) { Fail 'bcdedit write failed (record above is intact).' 1 }

$after = & $bcd /enum active
$ma = $after | Select-String 'hypervisorlaunchtype' | Select-Object -First 1
$curAfter = if ($ma) { $ma.Line.Trim() } else { '(absent - write did not stick!)' }
Write-Output ('AFTER:  {0}' -f $curAfter)
if ($curAfter -notmatch 'off') { Fail 'VERIFY FAILED: value is not off.' 1 }
Write-Output 'OK + verified. REBOOT when ready (nothing changes until reboot).'
Write-Output 'Revert: Set-HypervisorAuto.ps1 + reboot, or System Restore.'
