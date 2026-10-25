# 05 — Understanding: what GameLoop is + how your pieces connect

## What it is

GameLoop (Tencent, ex-TxGameAssistant; publisher string: Hong Kong Gathering Media Limited) = Android emulator + store/launcher for PC. You have v7.0.19.05 (ARP) / app build 7.0.159.0 + engine image 5.10.7000.123. It virtualizes Android (VHD-backed: `data.vhd` 11.6 GB), renders via EGL/GLES/Vulkan (`renderer/`), and runs CODM as an xapk (`GameLoopData\app\*.xapk`).

## Your setup (mapped)

```
Boot (Automatic)
  └─ GameLoopService (17200, :5284)
       ├─ GameLoop (1512, UI + :25120/:13900/:5283)
       ├─ GameLoopAssistant (15592)
       ├─ GameLoopDldSvr (13980, :58320 — updates)
       └─ GameLoopEmulator (8640, :15120 + ADB :5555)
            └─ GameLoopVm (20412, VM backend → data.vhd)
                 └─ CODM (com.activision.callofduty.shooter)
                      └─ keymaps: D:\CODM-Assets\NOTES\*.txt + GameLoopHub-BR-Global.zip
Recordings of play: D:\CODVIDS\2025\ + 2026\ (69 files, DO-NOT-DELETE per ACTIONS.md)
Tweaks (stale): D:\stuff\*.ps1/*.reg/*.bat → target dead exe names
```

## Risks / gotchas found live

1. **D: is FULL (0 free).** GameLoop updates + VHD growth + CODM patches will fail. Free space before anything else. Candidates already identified in audits: old ISOs (`D:\Os\`), duplicate GTA copies (check current state — audits freed ~6.4 GB in batch 1).
2. **ADB on 0.0.0.0:5555.** LAN-reachable debug bridge while emulator runs. Fine at home; don't run on untrusted networks. Same for :15120.
3. **Stale priority scripts.** IFEO `CpuPriorityClass` for AppMarket/AndroidEmulator does nothing on the new `GameLoop*` names; the .bat's `wmic` calls no-op. Either update or delete to avoid confusion.
4. **`FROM-ROOT` empty.** Audit batch 2 moved `gameloop cleanup.bat + GameLoop .lnk` there, but the dir reads empty now — verify if you cleaned it; the .lnk target is unknown (was "weird name, check target" in D-ROOT.md).
5. **Service always on.** Even with UI closed, GameLoopService runs (Automatic). If you want it off: `services.msc → GameLoopService → Manual`, or `sc config GameLoopService start=demand`.

## Suggested next steps (pick any)

- [ ] Free D: space (check `D:\Os\_OLD_ISOs` state, `Win+R → cleanmgr`)
- [ ] Update or remove the 3 stale priority files in `D:\stuff\`
- [ ] Resolve the missing `gameloop cleanup.bat` / GameLoop `.lnk` from FROM-ROOT (find or confirm deleted)
- [ ] If CODM keymaps matter: diff `NOTES\com.activision.callofduty.shooter*.txt` x9 vs current in-emulator mapping, keep one canonical copy
- [ ] Optional: `adb connect localhost:5555` → pull keymap/config directly from emulator to version in repo

Sources: live `Get-Process/Service/CIM/NetTCP/registry` 2026-09-20; disk measure 21.83 GB; `drive-audit/*.md`; `D:\stuff` file contents; `shooter-game/game.js:706-796`.
