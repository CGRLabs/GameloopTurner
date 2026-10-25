#Requires -Version 5.1
<#
.SYNOPSIS
  Dumps the CODM guest's (Android) established network connections via ADB.

.DESCRIPTION
  GameLoop's CODM traffic lives inside the VM, so host netstat can't see it.
  This script connects to the emulator (127.0.0.1:5555), reads /proc/net/tcp
  and /proc/net/udp, decodes the little-endian hex IPs/ports, and prints a
  readable table. Run it while in a match to capture the game server IPs.

  Read-only: does not change any state.

.EXAMPLE
  .\Get-CodmConnections.ps1
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

$adb = Get-ChildItem -Path 'C:\Program Files\Tencent','D:\Program Files\Tencent' -Filter 'adb.exe' -Recurse -ErrorAction SilentlyContinue |
       Select-Object -First 1 -ExpandProperty FullName
if (-not $adb) { Write-Error "adb.exe not found"; exit 1 }

function Decode-Ip([string]$hex) {
    # /proc/net stores IP as little-endian hex
    if ($hex.Length -ne 8) { return $hex }
    $b = for ($i = 6; $i -ge 0; $i -= 2) { [Convert]::ToInt32($hex.Substring($i,2),16) }
    return ($b -join '.')
}
function Decode-Port([string]$hex) { [Convert]::ToInt32($hex,16) }

function Decode-State([string]$st) {
    switch ($st) {
        '01' { 'ESTABLISHED' }
        '02' { 'SYN_SENT' }
        '03' { 'SYN_RECV' }
        '04' { 'FIN_WAIT1' }
        '05' { 'FIN_WAIT2' }
        '06' { 'TIME_WAIT' }
        '07' { 'CLOSE' }
        '08' { 'CLOSE_WAIT' }
        '09' { 'LAST_ACK' }
        '0A' { 'LISTEN' }
        '0B' { 'CLOSING' }
        default { $st }
    }
}

& $adb connect 127.0.0.1:5555 2>&1 | Out-Null

foreach ($proto in 'tcp','udp') {
    Write-Host ''
    Write-Host "=== /proc/net/$proto ===" -ForegroundColor Cyan
    $raw = & $adb -s 127.0.0.1:5555 shell "cat /proc/net/$proto" 2>&1
    $rows = foreach ($line in $raw) {
        $parts = $line -split '\s+' | Where-Object { $_ }
        if ($parts.Count -lt 4) { continue }
        if ($parts[0] -eq 'sl') { continue }
        $local = $parts[1] -split ':'
        $rem   = $parts[2] -split ':'
        $st    = $parts[3]
        $uid   = if ($parts.Count -ge 8) { $parts[7] } else { '' }
        [pscustomobject]@{
            Proto    = $proto.ToUpper()
            LocalIP  = Decode-Ip $local[0]
            LocalPort= Decode-Port $local[1]
            RemoteIP = Decode-Ip $rem[0]
            RemotePort= Decode-Port $rem[1]
            State    = Decode-State $st
            UID      = $uid
        }
    }
    # show only established/connected or listening on non-loopback remotes
    $rows | Where-Object { $_.RemoteIP -ne '0.0.0.0' -and $_.RemoteIP -ne '127.0.0.1' } |
            Format-Table -AutoSize
    $rows | Where-Object { $_.RemoteIP -eq '0.0.0.0' -and $_.State -eq 'LISTEN' } |
            Select-Object -First 5 | Format-Table -AutoSize | Out-Null
}

Write-Host ''
Write-Host "Tip: run this while in a match. The external IP that appears only during" -ForegroundColor Yellow
Write-Host "gameplay (and has a high port) is likely your game server. Ping it with:" -ForegroundColor Yellow
Write-Host "  Test-Connection <ip> -Count 10" -ForegroundColor Yellow
Write-Host "Then pass it to Monitor-Connection.ps1 -ServerIP <ip>." -ForegroundColor Yellow