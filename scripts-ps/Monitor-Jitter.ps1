#Requires -Version 5.1
<#
.SYNOPSIS
  Live jitter/loss visualiser for GameLoop/CODM sessions.

.DESCRIPTION
  Pings a target every second and renders a rolling ASCII sparkline of latency
  plus a jitter (max-min) and loss counter. Designed to run beside the game so
  you can *see* spikes as they happen instead of reading numbers after the fact.

  Read-only: does not change any system state.

.PARAMETER Target
  Host to ping (default 1.1.1.1). Use your game server IP for the most relevant
  view (get it via Get-CodmConnections.ps1).

.PARAMETER Window
  Number of samples in the rolling window (default 60 = 1 minute at 1s).

.EXAMPLE
  .\Monitor-Jitter.ps1 -Target 1.1.1.1
  .\Monitor-Jitter.ps1 -Target 43.130.54.101
#>
[CmdletBinding()]
param(
    [string]$Target = '1.1.1.1',
    [int]$Window = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

$spark = @(' ', '.', ':', '-', '=', '+', '*', '#', '%', '@')
$buf = [System.Collections.Generic.List[double]]::new()
$loss = 0
$total = 0
$min = [double]::MaxValue
$max = 0.0

function Draw-Bar([double]$v, [double]$scale) {
    $idx = [math]::Floor($v / $scale)
    if ($idx -lt 0) { $idx = 0 }
    if ($idx -gt 9) { $idx = 9 }
    return $spark[$idx]
}

Write-Host "Jitter monitor -> $Target  (window=$Window)  Ctrl+C to stop" -ForegroundColor Green
Write-Host ''

try {
    while ($true) {
        $total++
        $r = Test-Connection -ComputerName $Target -Count 1 -ErrorAction SilentlyContinue
        if ($r) {
            $ms = [double]$r.ResponseTime
            $buf.Add($ms)
            if ($ms -lt $min) { $min = $ms }
            if ($ms -gt $max) { $max = $ms }
            $glyph = Draw-Bar $ms 10.0
            $color = if ($ms -gt ($min + 30)) { 'Red' } elseif ($ms -gt ($min + 15)) { 'Yellow' } else { 'Green' }
            Write-Host -NoNewline "$glyph" -ForegroundColor $color
        } else {
            $loss++
            $buf.Add([double]::NaN)
            Write-Host -NoNewline 'X' -ForegroundColor Red
        }
        if ($buf.Count -gt $Window) { $buf.RemoveAt(0) }

        # rolling stats
        $valid = $buf | Where-Object { -not [double]::IsNaN($_) }
        if ($valid.Count -gt 1) {
            $avg = [math]::Round(($valid | Measure-Object -Average).Average, 1)
            $jitter = [math]::Round(($valid | Measure-Object -Maximum).Maximum - ($valid | Measure-Object -Minimum).Minimum, 1)
            $lossPct = [math]::Round($loss / $total * 100, 1)
            $line = "`r  avg=$avg ms  jitter=$jitter ms  loss=$lossPct%  min=$min max=$max  (n=$total)        "
            Write-Host -NoNewline $line
        }

        if ($total % $Window -eq 0) {
            Write-Host ''
            Write-Host "---- window reset ----"
            $min = [double]::MaxValue
            $max = 0.0
        }

        Start-Sleep -Seconds 1
    }
} catch [System.Management.Automation.PipelineStoppedException] {
    # Ctrl+C
} finally {
    Write-Host ''
    Write-Host ''
    $valid = $buf | Where-Object { -not [double]::IsNaN($_) }
    if ($valid.Count -gt 0) {
        $avg = [math]::Round(($valid | Measure-Object -Average).Average, 1)
        $jitter = [math]::Round(($valid | Measure-Object -Maximum).Maximum - ($valid | Measure-Object -Minimum).Minimum, 1)
        Write-Host "Summary: samples=$total  avg=$avg ms  jitter=$jitter ms  loss=$([math]::Round($loss/$total*100,1))%" -ForegroundColor Cyan
    }
}