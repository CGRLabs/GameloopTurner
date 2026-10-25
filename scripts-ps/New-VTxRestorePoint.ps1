#Requires -RunAsAdministrator
<#
.SYNOPSIS
  Safety net for the VT-x experiment: enables System Protection on
  C: if off, then creates a manual restore point. Changes nothing else.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File New-VTxRestorePoint.ps1

.REVERT
  N/A (this script only ADDS protection + a restore point). To roll the whole
  machine back: System Properties -> System Protection -> System Restore.
#>
[CmdletBinding()]
param(
  [string]$Description = 'Before VTx test (hypervisor off)'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Fail([string]$Message, [int]$Code) {
  [Console]::Error.WriteLine($Message)
  exit $Code
}

# 1. Protection on C: (idempotent call, needs no reboot, reversible via UI).
try {
  Enable-ComputerRestore -Drive 'C:\'
  Write-Output 'System Protection on C: ensured.'
} catch {
  Fail ('Enable-ComputerRestore failed: {0}' -f $_.Exception.Message) 1
}

# 2. Checkpoint (one per 24h limit: reuse notice if Windows refuses).
try {
  Checkpoint-Computer -Description $Description -RestorePointType 'MODIFY_SETTINGS'
  Write-Output 'Restore point requested.'
} catch {
  Fail ('Checkpoint failed (one-per-day limit or service off): {0}' -f $_.Exception.Message) 1
}

Start-Sleep -Seconds 5
$pts = Get-ComputerRestorePoint -ErrorAction SilentlyContinue |
  Sort-Object CreationTime -Descending | Select-Object -First 3
if (-not $pts) { Fail 'No restore points visible after checkpoint.' 1 }
Write-Output 'Recent restore points:'
foreach ($r in $pts) { Write-Output ('  {0} :: {1}' -f $r.CreationTime, $r.Description) }
Write-Output 'OK. Rollback path: System Properties -> System Protection -> System Restore.'
