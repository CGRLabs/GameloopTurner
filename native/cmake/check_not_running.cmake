# ─────────────────────────────────────────────────────────────────────────────
# Pre-link guard for GameLoopTuner.exe.
#
# Running `cmake --build` while GameLoopTuner.exe is still executing ends in
#
#   LINK : fatal error LNK1104: cannot open file 'bin\GameLoopTuner.exe'
#
# which tells the developer nothing about *why* the file is busy. With the
# requireAdministrator manifest the process is elevated, so an unelevated build
# shell cannot even enumerate its handle to diagnose it — the lock is invisible.
#
# This script runs as a PRE_LINK command, detects the situation, and either
# asks the running instance to exit politely or fails with a message that says
# what to do.
#
# It never force-kills. A tuner mid-Apply-All holds a database transaction
# open, and TerminateProcess would drop it.
#
# NOTE: PowerShell is invoked by name rather than discovered with find_program.
# find_program is unreliable in `cmake -P` script mode — it returns NOTFOUND
# here even for powershell.exe, and the guard would then silently succeed and
# the build would fail with LNK1104 anyway. powershell.exe is present on every
# Windows installation that can run MSBuild.
# ─────────────────────────────────────────────────────────────────────────────

if(NOT DEFINED PROJECT_TUNER_EXE)
    return()
endif()

if(NOT EXISTS "${PROJECT_TUNER_EXE}")
    return()
endif()

execute_process(
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_LIST_DIR}/check_not_running.ps1"
            -ExePath "${PROJECT_TUNER_EXE}"
    RESULT_VARIABLE rc)

if(rc EQUAL 0)
    # Nothing running, or it exited politely. The link can proceed.
    return()
endif()

message(FATAL_ERROR
    "cannot link ${PROJECT_TUNER_EXE}: the previous GameLoopTuner.exe is "
    "still running, so the linker cannot replace it.\n"
    "Close the GameLoop Tuner window (or Stop-Process -Name GameLoopTuner "
    "-Force) and rebuild.")
