# 03 — Repo + D: traces of GameLoop

## A. Priority tweak scripts (D:\stuff\) — STALE names

| File | Size | Content summary |
|---|---|---|
| `D:\stuff\check_gameloop_priority.ps1` | 397 B | loops `Get-Process -Name aow_exe`, prints PriorityClass |
| `D:\stuff\gameloop_priority.reg` | 444 B | forces `CpuPriorityClass=3` (High) for `AppMarket.exe` + `AndroidEmulator.exe` via IFEO PerfOptions |
| `D:\stuff\set_gameloop_priority.bat` | 450 B | `wmic ... AppMarket.exe / AndroidEmulator.exe / aow_exe.exe CALL setpriority "high priority"` |

Problem: none of `AppMarket/AndroidEmulator/aow_exe` exist as running processes anymore — current names are `GameLoop*.exe` (6 procs, see 01). These scripts silently do nothing now. To fix: retarget to `GameLoopEmulator` / `GameLoopVm` / `GameLoop`, or use `Get-Process GameLoop* | % PriorityClass='High'`.

## B. D:\CODM-Assets\ — keymaps + hub

- `GameLoopHub-BR-_-Global.zip` (root of CODM-Assets)
- `NOTES\`: CORRECTION 2026-09-20 — these are NOT human-readable notes. They are GameLoop **`<KeyMapFileData>` export files**: embedded JPEG screenshot + full `<Switch>` dictionary + key bindings. Restorable via import, not readable as text. Contents: `BR - GameLoopHub.txt` (147KB), `GameLoopHub - BR _ Global.txt` (175KB), `GameLoopHub - MP _ Global.txt` (387KB), `GameLoopHub-BR.txt`, plus `com.activision.callofduty.shooter*.txt` x~9 all ~136KB (near-identical repeated exports of one layout = dupes), `Killcodm.txt`, `WORLDWIDE.txt`, `games to get.txt`. The `.zip` just contains the BR export.
- History per `drive-audit/ACTIONS.md` batch 2+4: `D:\Games\BR.txt + MP.txt` (GameLoop keymaps for CODM shooter) → `D:\Codm\` → merged into `D:\CODM-Assets\`; `Emulador-rar 494MB, SmartTool-2.0.4, GameLoopHub, MODS\, NOTES\(+14), UI\` came from pastwindows dissolve. So CODM-Assets is your dev-asset home, GameLoop is the runtime that consumes these keymaps.

## C. drive-audit history (D:\Projects\forfun\drive-audit\)

- `BIG-STORAGE.md:33-35`: `D:\Program Files\` = single child `Tencent\` (GameLoop emulator), "data-drive copy, not C:\Program Files" — still true today.
- `D-ROOT.md:47`: `gameloop cleanup.bat` sat in D:\ root (2025-10-26) — "check content".
- `MISC.md:10,29`: `*.reg (gameloop_priority)`, `*.bat/.ps1 (gameloop)` in `D:\stuff\`; root tinies `gameloop cleanup.bat`, `.lnk` → read then `stuff\` or delete.
- `ACTIONS.md` batch 2: `gameloop bat, GameLoop .lnk` + others → `D:\stuff\FROM-ROOT\`. NOTE: today `D:\stuff\FROM-ROOT\` listing came back empty — either moved again or cleaned since 2026-09-12. The 3 priority files in `D:\stuff\` root are the surviving copies.

## D. Repo grep hits (code, not emulator)

- `shooter-game/BRIEF.md:16`: "game loop in `game.js` + `index.html` skeleton" (Alice core) — this is the *code* game loop, covered in 04.
- `shooter-game/debug.js:29`: perf state updated via `LOG.frame` from game loop.
- No `*loop*` filenames matched glob; no Python gameloop code in repo (`**/*.py` list is all pony-studio/pocket-tts/ai-learning-lab).
