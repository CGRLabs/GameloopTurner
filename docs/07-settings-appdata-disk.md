# 07 — AppData + disk settings: where GameLoop REALLY keeps your CODM config

## Map

```
%APPDATA%\Tencent\
  beacon\            Androws.db, QmCache.db, GlobalMgr.db      <- telemetry/crash (Bugly/Beacon)
  BeaconConfig\      0WIN06LBIJ5ZBEUW.ini (logid counters)     <- per-binary telemetry ids
  DeskUpdate\, DtConfig\  GlobalMgr.db / seq timestamps        <- updater + datatype cfg
  qimei\             (empty today)                             <- device fingerprint staging (real qimei lives in reg Env + Configs.db sdid/oaid)
  GameLoop\          <- the live one (1046 files)
    ams_cache\       AmsWinSdk_2026-09-19/20.log               <- login/session SDK logs
    apps\
      com.activision.callofduty.shooter\  (.ico+.png only)     <- store icons; REAL per-app config is in db (below)
      com.ceyhan.sets\ / com.yjlink.secretsettings\ (icons)   <- sideloaded side apps — check what these are in the store UI
    cef\             CEF_GameLoop\ + CEF_GameLoopEmulator\     <- Chromium profiles for store browser + emulator overlay UI
    config\
      7f18….dat (+ cert_hash in reg)                           <- device cert — don't touch
      com.activision.callofduty.shooter\smk.conf               <- smart-key texture templates (same blob as db id 413)
    db\
      Configs.db (+ shm/wal)                                   <- MASTER settings DB (decoded below)
      bc_/dt_0WIN06….db                                        <- beacon/datatype aux DBs
    offline-page\, context_menu\                               <- store offline bundle + shell-ext staging

D:\Program Files\Tencent\GameLoopData\
  app\config\  com.activision.callofduty.shooter.conf          <- guest-side CODM flags (JSON, readable)
               com.tencent.androws.conf                        <- ENGINE config (huge JSON, readable — the good stuff)
               oat_file.conf / sdid.conf
  Component\GameLoop\  Config.json / Androws.json / deviceConfig.json / pc_app_config.json / Setup.json / file_config.json / GameLoopDldSvr.cfg / qt.conf / qtlogging.ini / bugly_stringtable.ini / vk_swiftshader_icd.json / renderer\*.json
  Component\3d62a0…_5.10.7000.123_…\  Setup.json / file_config… <- engine payload staging
```

## Configs.db — master DB (54 `configs` rows + keymap tables)

Tables: `configs`, `global_config`, `key_map_configs_cache_official` (24 rows), `key_map_aux_setting`, `migrate_status_tb`, `key_map_screenshot`.

Per-app pattern (5 rows each): `host_config` (Windows-side window/behaviour) / `guest_config` (Android-side) / `global_config` / `default.key_mapping` / `smk`.

### YOUR CODM rows (ids 409-413, 426, 433)

- **409 host_config**: `tp:true` (texture-pipe on), `auto_fullscreen:false`, `rotation:0`, **`default_frame_rate:120, maximum_frame_rate:240`**, functionbar items [104,108,206,215], red-dot config. So the emulator allows up to 240 but defaults CODM to 120.
- **410 guest_config** (== the `.conf` file): `GPU_PSS {ETC,ASTC}`, `multipane disabled/landscape`, `softvideo:true` (CPU video decode — safer compat, costs CPU; if CODM video/streams stutter with your 6c/10GB, this flag is a suspect).
- **411 global_config**: `{}` — no global override for CODM.
- **412 default.key_mapping**: empty — you run on user/official cached maps, not the default slot.
- **433 local_user_config — YOUR LIVE CODM SETTINGS**:
  `frame_rate:120, clarity:3, user_select_clarity:true, low_latency_mode:true, default_frame_gen_on:false, aspect_ratio:100, hide_full_screen_topbar:true, is_toolbar_open:false, landscape_render_wnd_size:[916,515], monitor_size:[1280,976], monitor_dpi:1.0, orientation:0 (landscape), special_aspect_ratio:[5,4], app_close_action:ask-first`
  → 120fps, clarity level 3 (your manual pick), low-latency ON, frame-gen OFF, toolbar hidden, render window 916x515 inside a 1280x976 monitor box.
- **426 scenes_config**: `{"scenes":[]}` — no custom keymap scenes; official ones rule.
- **aux**: `key_map_aux_setting` has one CODM row `(…,0,28091,…,0,…)` — aux flags/version stamp for the mapper.

### Keymap system (how your BR/MP maps are stored)

`key_map_configs_cache_official` keys for CODM:
- modes: **145** (keyboard — 4 solutions: `1768790151264` "Multi Player" official, `…7209` mpkeymap, `…9885` prophunt, `…7993` brkeymap — last = brkeymap), **146** (51KB blob — aim/mouse layer), **531686** (15KB — your custom set, solution 1063553)
- `recentlyused_mode` = {145:…, 146:…, 531686:…} all touched within minutes today; **`lastmodeid` = 531686 → your ACTIVE map is the custom 531686 set**
- `offcial_config2` = Switch→Texture table (DoorOpen→Tex1 开关门, GetIntoCar→Tex2…, Run→疾跑, Sniper→瞄准, Jump→跳跃, Reload→换弹药…). This is the dictionary the mapper speaks.
- `keymap_data.*` blobs (8–51KB) = the actual key→screen-position bindings per mode/solution. Your `D:\CODM-Assets\NOTES\*.txt` + `GameLoopHub-BR-Global.zip` are human backups/exports of this same data — if you ever wipe Configs.db, those txts are the recovery path.

### smk.conf — smart-key image matching

`Tex id=10001…10157 + 30013…` entries with `c1/c2` hashes, cut sizes, and Chinese `comm` labels (开关门 door, 上车驾驶 drive, 跳伞 parachute 1/4-man, 疾跑 sprint, 探头 lean, 瞄准 ADS, 换弹药 reload, 救人 revive, 刹车 brake, 上/下飞 fly, 游泳 swim, etc.). The emulator screenshots the game, hashes regions, and auto-switches key scheme when e.g. "in vehicle" or "parachuting" textures appear. `check="1"` rows are verification anchors. Not user settings — recognition data. Same content sits in db id 413 (UTF-8) and `%APPDATA%…\smk.conf` (mojibake encoding — read the db copy).

### Identity rows

- `androws.sdid`: `seed_id bb4ece…`, `vdid ab8f90…`, `oaid ""` (+ `androws.oaid` row) — device/account fingerprint triple. oaid empty = no mobile-OAID on PC (expected). Don't share seed/vdid publicly.
- `global_config`: pkg `com.tencent.androws`, split-screen bg URL.

### Beacon / qimei / cert (the "and and and")

- `beacon\*.db` + `BeaconConfig\*.ini` (`[GameLoopEmulator.exe] logid=6727` etc.) = Tencent Beacon telemetry counters per binary. Harmless, grows slowly.
- `qimei\` empty on disk, but qimei **values** live in reg `Env\V*Dioa` blobs + `Duration\QimeiChangeCheck` — the device fingerprint the store/login uses.
- `config\7f18….dat` + reg `cert_hash` = device certificate for login attestation. Lose it → re-login + new attestation.
- `ams_cache\AmsWinSdk_*.log` = login SDK logs — first place to look when "login failed" happens.

## GameLoopData engine JSONs (readable, high-signal)

- **`app\config\com.tencent.androws.conf`** (the big one): auto-granted Android permissions (storage, mic, camera, location…), `dns: [114.114.114.114, 119.29.29.29]` (Chinese DNS — if CODM login/DNS feels slow from your location, override at Windows level), `named_filter` + `vattr_list` hiding `/dev/qemu/vbox/houdini/arm` paths from apps (emulator anti-detection vs CODM checks), `mock telephony/hardware` on, `dpiPatch` on, share-folder mounts DCIM/Pictures/Music/Movies/Documents/Download/AppRecoders → host ports 5813–5819 with watch dirs, `minimum_required_space:5 / required_disk_space:10` (GB — **you violate this with D: at 0 free**), ANR/reporting/perf-sample knobs, boot-broadcast blocklists, `forcesoftvideo:true` + `default_render_type:9`.
- **`Component\GameLoop\Androws.json`**: VM `TVM_3000` port 3000, `multiDisplay:true`, `glTransport:"wormhole"`, fence on, gfx-stream on, hyperv vsock 5716.
- **`Config.json`**: store window 1200x750 (min 1200x600), URL `https://webclient.gameloop.com/`.
- **`deviceConfig.json`**: `forceOpenGL:false`.
- **`renderer\gpu_blacklist.json + fg_gpu_whitelist.json`**: which GPUs are forced off/on GL paths — your RX 5700 XT is NOT blacklisted (GL 4.6 + Vulkan 1.x both probed OK in reg).
- **`GameLoopDldSvr.cfg / pc_app_config.json / Setup.json / file_config*.json / qt.conf / qtlogging.ini / bugly_stringtable.ini`**: downloader, PC-app bridge, installer manifest, Qt platform, crash-symbol table. Read-only unless debugging update/install failures.

## What to actually tweak for CODM (safe list)

1. In UI or reg `Settings\Common`: cores 6 / RAM 10GB is generous for 8c/16GB host — if Windows stutters while CODM runs, drop to 4c/8GB (your tier table says recommend is 4c/6GB).
2. In UI or db 433: `frame_rate` 120 + `low_latency` true is already the competitive pick; `clarity` 3 is your manual choice — raise only if GPU headroom (RX 5700 XT, 8.5GB VRAM — fine).
3. `ShowFps` 0→1 (reg) for the in-emulator counter while tuning.
4. Keymaps: active = custom 531686. Back up `Configs.db` before editing maps in UI; your NOTES Goods txts are the second backup.
5. DON'T hand-edit: `smk` blobs, `sdid/vdid/cert`, `named_filter/vattr` (breaks anti-detection), DNS inside androws.conf (override in Windows instead).
6. Free D: space first — `required_disk_space:10` means the engine considers <10GB unsafe; you're at 0.
