#Requires -RunAsAdministrator
<#
.SYNOPSIS
  VT-x experiment REVERT: boots Windows hypervisor again (bcdedit
  hypervisorlaunchtype auto). Takes effect after reboot.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File Set-HypervisorAuto.ps1
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Fail([string]$Message, [int]$Code) {
  [Console]::Error.WriteLine($Message)
  exit $Code
}

# Same 32-bit-shell redirect trap as Set-HypervisorOff: resolve explicitly.
$bcd = Join-Path $env:SystemRoot 'System32\bcdedit.exe'
if (-not (Test-Path -LiteralPath $bcd)) {
  $bcd = Join-Path $env:SystemRoot 'Sysnative\bcdedit.exe'
}
if (-not (Test-Path -LiteralPath $bcd)) { Fail 'bcdedit.exe not found.' 1 }

& $bcd /set "{current}" hypervisorlaunchtype auto | Out-Null
if ($LASTEXITCODE -ne 0) { Fail 'bcdedit write failed.' 1 }
$ma = ((& $bcd /enum active) | Select-String 'hypervisorlaunchtype' | Select-Object -First 1)
$cur = if ($ma) { $ma.Line.Trim() } else { '(absent - write did not stick!)' }
Write-Output ('NOW: {0}' -f $cur)
if ($cur -notmatch 'auto') { Fail 'VERIFY FAILED.' 1 }
Write-Output 'OK + verified. REBOOT to restore hypervisor (GameLoop/HVCI back).'
