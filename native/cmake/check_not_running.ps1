# Invoked by cmake/check_not_running.cmake before linking GameLoopTuner.exe.
# Exits non-zero when the executable is running, so the build fails with a
# useful message instead of LINK : fatal error LNK1104.

param(
    [Parameter(Mandatory = $true)][string]$ExePath
)

$name = [IO.Path]::GetFileNameWithoutExtension($ExePath)

$processes = Get-Process -Name $name -ErrorAction SilentlyContinue
if (-not $processes) {
    # Nothing running. Also treat "process gone between check and link" as OK.
    exit 0
}

$ids = ($processes | ForEach-Object { "PID $($_.Id)" }) -join ', '
$title = ($processes | Select-Object -First 1).MainWindowTitle

# Ask politely first: CloseMainWindow sends WM_CLOSE, which the app turns into
# PostQuitMessage. That gives the drift thread and any open SQLite transaction
# a chance to unwind.
$closed = $false
foreach ($p in $processes) {
    try {
        if ($p.CloseMainWindow()) {
            $closed = $true
        }
    } catch {
        # A windowless or already-dead process: fall through to the wait.
    }
}

if ($closed) {
    # Give it up to 4 s to save state and exit.
    for ($i = 0; $i -lt 8; $i++) {
        Start-Sleep -Milliseconds 500
        $still = Get-Process -Name $name -ErrorAction SilentlyContinue
        if (-not $still) { exit 0 }
    }
}

$alive = Get-Process -Name $name -ErrorAction SilentlyContinue
if (-not $alive) {
    exit 0
}

$aliveIds = ($alive | ForEach-Object { "PID $($_.Id)" }) -join ', '

# The mutex can outlive the window if the process is mid-startup or has
# crashed. Report that specifically: a force-kill here would be the fix, but
# it is the developer's call because the process may hold a DB write.
if ([string]::IsNullOrWhiteSpace($title)) {
    Write-Host ""
    Write-Host "GameLoop Tuner is running without a window ($aliveIds)." -ForegroundColor Yellow
    Write-Host "It is probably mid-startup or has crashed and leaked its mutex." -ForegroundColor Yellow
    Write-Host "Stop it with:  Stop-Process -Name $name -Force"
    Write-Host ""
    exit 1
}

Write-Host ""
Write-Host "GameLoop Tuner is still running ($aliveIds)." -ForegroundColor Yellow
Write-Host "Window: '$title'"
Write-Host ""
Write-Host "The build cannot replace an executable that is executing, so" -ForegroundColor Yellow
Write-Host "the link will fail with a bare LNK1104. Close the window" -ForegroundColor Yellow
Write-Host "(or Stop-Process -Name $name -Force) and rebuild." -ForegroundColor Yellow
Write-Host ""

exit 1
