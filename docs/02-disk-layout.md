# 02 — Disk layout

## Totals

- `D:\Program Files\Tencent\` total: **~21.83 GB** (measured 2026-09-20, recursive file sum)
- `D:` drive: **516.7 GB used / 0 GB free** — FULL. GameLoop is ~4% of D: but any update/VHD growth will fail until you free space.
- Biggest single file: `GameLoop\Image\data.vhd` **11.6 GB** (Android system/data image) + `swap.vhd` 0.04 GB.

## Tree (depth 2, live)

```
D:\Program Files\Tencent\
  GameLoop\
    Application\
      7.0.159.0\            <- current build (renderer/, resources/, offline-page/_next/, tencent_game_ai/, trtc/, platforms/, styles/, win11_context_menu/)
      context_menu\         <- shell extension (PCyybContextMenu.dll + App.exe + .msix x4)
      apk.ico
      GameLoopLauncher.exe
      Uninstall.exe
    Image\
      5.10.7000.123\        <- engine version dir
      data.vhd  (11.6 GB)
      swap.vhd  (0.04 GB)
  GameLoopData\
    app\                    <- xapk cache (169a95...xapk, 5c4c8f...xapk, be819e...), config/files/icons/libs/logs/notification/, foo/
    attachments/
    Component\              <- 7z payloads (GameLoop.7z, 094ceb...7z, 241192...offline202609172009.7z, 3d62a0..._5.10.7000.123_OVqkpp0m[.7z])
    rdelivery\mmkv\
    reports/
    reshub\mmkv,patch,resource\
```

Application\7.0.159.0 highlights: `renderer\AndrowsFG.exe + libEGL/libGLESv2/vulkan-1.dll`, `resources\reg\*.reg (storvsp/vdrvroot/Vid/vmbusr/vmsmp/vpcivsp)`, `resources\script\fix_hyperv.bat + fix_vmp.bat`, `trtc\liteav*.dll` (screen share/rtc), full `offline-page` Next.js UI bundle.

## AppData

`C:\Users\SKULL\AppData\Roaming\Tencent\GameLoop\` exists (per-user config/logs — listing returned empty via one probe, re-check with dir /a if needed).
`C:\Users\SKULL\AppData\Local\Tencent\` missing.

## Related on-disk neighbours

- `D:\CODM-Assets\` — your CODM sidecar (GUNS/, MODS/, NOTES/ keymaps, GameLoopHub-BR-Global.zip). Details in 03.

- `D:\stuff\check_gameloop_priority.ps1`, `gameloop_priority.reg`, `set_gameloop_priority.bat` — stale priority tweaks. Details in 03.
