#Requires -Version 5.1
<#
.SYNOPSIS
  Live connection monitor for GameLoop/CODM sessions.

.DESCRIPTION
  Samples gateway, public, and (optional) game-server ping + DNS resolve time +
  per-process TCP connection counts, and appends one row per sample to a CSV.
  Run it in a window beside the game, play a match, then Ctrl+C to stop.
  Analyse the CSV in Excel / pandas to find spikes, loss, jitter.

  Does NOT change any system state (read-only).

.PARAMETER ServerIP
  Game server IP (get it via: netstat -ano | findstr :443 while in a match).
  Optional; if omitted, server ping is skipped.

.PARAMETER Interval
  Seconds between samples (default 5).

.PARAMETER LogPath
  CSV output path (default: .\conn-log-<stamp>.csv next to this script).

.EXAMPLE
  .\Monitor-Connection.ps1 -ServerIP 203.0.113.45 -Interval 5
#>
[CmdletBinding()]
param(
    [string]$ServerIP = '',
    [int]$Interval = 2,
    [int]$PingCount = 2,
    [string]$LogPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

if (-not $LogPath) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $LogPath = Join-Path $PSScriptRoot "conn-log-$stamp.csv"
}

# --- resolve gateway once ---
$gateway = $null
try {
    $route = Get-NetRoute -DestinationPrefix '0.0.0.0/0' -ErrorAction Stop |
             Sort-Object RouteMetric | Select-Object -First 1
    $gateway = $route.NextHop
} catch {
    Write-Warning "Could not detect default gateway: $_"
}

# --- CSV header ---
$header = 'Timestamp,Gateway,ServerIP,'
$header += 'PingGW_ms,PingGW_Loss,PingPub_ms,PingPub_Loss,'
$header += 'PingSrv_ms,PingSrv_Loss,DNS_ms,'
$header += 'GameLoop_Conns,CODM_Conns,Total_TCP_Conns'
if (-not (Test-Path -LiteralPath $LogPath)) {
    $header | Set-Content -LiteralPath $LogPath -Encoding UTF8
}

function Measure-Ping([string]$Target, [int]$Count = $PingCount) {
    # Returns "avg_ms,loss_pct"
    if (-not $Target) { return ', ' }
    try {
        $r = Test-Connection -ComputerName $Target -Count $Count -ErrorAction Stop
        $avg = [math]::Round(($r | Measure-Object -Property ResponseTime -Average).Average, 1)
        $loss = [math]::Round((($Count - $r.Count) / $Count) * 100, 0)
        return "$avg,$loss"
    } catch {
        return 'timeout,100'
    }
}

function Measure-Dns {
    try {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        [void][System.Net.Dns]::GetHostAddresses('activision.com')
        $sw.Stop()
        return [math]::Round($sw.Elapsed.TotalMilliseconds, 1)
    } catch { return '' }
}

function Count-TcpByProcess([string]$NamePattern) {
    try {
        $conns = Get-NetTCPConnection -ErrorAction SilentlyContinue |
                 Where-Object { $_.State -eq 'Established' }
        $pids = (Get-Process | Where-Object { $_.Name -match $NamePattern }).Id
        return ($conns | Where-Object { $pids -contains $_.OwningProcess }).Count
    } catch { return '' }
}

Write-Host "Monitoring started. Log: $LogPath" -ForegroundColor Green
Write-Host "Gateway: $gateway   ServerIP: $(if ($ServerIP) {$ServerIP} else {'(none)'})   Interval: ${Interval}s   PingCount: $PingCount"
Write-Host "NOTE: many routers block ICMP, so 'timeout,100' for the gateway is normal and not a real problem."
Write-Host "Press Ctrl+C to stop."
Write-Host ''
Write-Host ('{0,-20} {1,8} {2,8} {3,8} {4,8} {5,6} {6,6}' -f 'Time','GW_ms','Pub_ms','Srv_ms','DNS_ms','GLc','TCPc')

try {
    while ($true) {
        $ts = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
        $gwPing = Measure-Ping $gateway
        $pubPing = Measure-Ping '1.1.1.1'
        $srvPing = Measure-Ping $ServerIP
        $dns = Measure-Dns
        $glc = Count-TcpByProcess 'GameLoop'
        $codmc = Count-TcpByProcess 'aow_exe|AndroidProcess|GameLoopEmulator'
        $total = (Get-NetTCPConnection -ErrorAction SilentlyContinue | Where-Object State -eq 'Established').Count

        $row = "$ts,$gateway,$ServerIP,$gwPing,$pubPing,$srvPing,$dns,$glc,$codmc,$total"
        $row | Add-Content -LiteralPath $LogPath -Encoding UTF8

        # console summary
        $gwMs = ($gwPing -split ',')[0]
        $pubMs = ($pubPing -split ',')[0]
        $srvMs = ($srvPing -split ',')[0]
        Write-Host ('{0,-20} {1,8} {2,8} {3,8} {4,8} {5,6} {6,6}' -f $ts,$gwMs,$pubMs,$srvMs,$dns,$glc,$total)

        Start-Sleep -Seconds $Interval
    }
} catch [System.Management.Automation.PipelineStoppedException] {
    # Ctrl+C
} finally {
    Write-Host ''
    Write-Host "Stopped. Log saved to: $LogPath" -ForegroundColor Cyan
    Write-Host "Analyse with Excel / pandas: look for spikes in PingSrv_ms and DNS_ms columns."
}