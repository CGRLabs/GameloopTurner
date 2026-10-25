# GameLoopHub Research Report

## 0. Summary / compatibility verdict

**Target:** GameLoop install at `C:\ARENA\GameLoop` (version `7.0.19.01`, build `7.0.107.0`, engine image `5.10.1900.91`), AppData root `%APPDATA%\Tencent\GameLoop`. Vendor folder name is **`GameLoop`** (not `GameLoopHub`), but the install is a multi-app "Hub"-style build (installs CODM, Arena Breakout, PUBG Mobile, etc. from one client).

**Verdict: ADAPTABLE WITH TARGETED CHANGES — not drop-in.** Most of the tuner's hardcoded surfaces are **SAME** (AppData DB path, `smk.conf` path, CODM package, HKCU registry base, `Settings\Common`, mode IDs 145/146/531686, BR/DMZ flat solution IDs 146/1063553, process names, `ano_tmp`). But several are **DIFFERENT/ABSENT** and need code changes: install root is not under the scanned candidate paths (found via `InstallPath` in the 64-bit HKLM branch), the official cache table has `value0..value4`, `configs` has extra columns, `migrate_status_tb`/`recentlyused_mode`/`scenes_config` are absent, a new BR solution `GameLoopHub.com` (1792331375184) exists, `low_latency_mode`/`default_frame_gen_on`/`30004` are absent, `ano_tmp_quarantine` is absent, and new processes (`GLABox*`, `AceKeeper*`) are not covered by `is_gameloop_image()`/kill order. No secret values are printed.

## 1. System context

| Item | Value |
|---|---|
| OS | Microsoft Windows 11 Pro 10.0.26300, 64-bit |
| CPU | AMD Ryzen 5 7600, 6 cores / 12 threads |
| RAM | 32 GB (22.7 GB free at capture) |
| Fixed drives | C: 930.4 GB (583.9 free), D: 465.7 (12.6 free), E: 465.8 (20.9 free) |
| Current user | `VOVKA` (not elevated / not admin) |
| ARP (GameLoop) | `HKLM\...\Uninstall\GameLoop`, `DisplayVersion 7.0.19.01`, no `InstallLocation` |

## 2. Installation & paths

| Item | Value |
|---|---|
| Install root | `C:\ARENA\GameLoop` (from HKLM `InstallPath`) |
| GameLoopData | `C:\ARENA\GameLoopData` (from HKLM `GameLoopData`) |
| Version dir | `C:\ARENA\GameLoop\Application\7.0.107.0` |
| Launcher exe | `C:\ARENA\GameLoop\Application\GameLoopLauncher.exe` (also root copy) |
| Main exes | `GameLoop.exe`, `GameLoopEmulator.exe`, `GameLoopVm.exe`, `GameLoopAssistant.exe`, `GameLoopDldSvr.exe`, `GameLoopService.exe`, `GameLoopLauncher.exe`, `CefRendererProcess.exe`, `adb.exe`, `GLABoxHeadless.exe`, `GLABoxSVC.exe`, `AceKeeper.exe`, `AceKeeperService.exe`, `GameLoopVfs.exe` |
| `adb.exe` | present in version dir (6.0 MB) |
| `ano_tmp` | `C:\ARENA\GameLoop\Application\7.0.107.0\ano_tmp` (present) |
| `ano_tmp_quarantine` | ABSENT |

Registry install keys (HKLM `SOFTWARE\Tencent\GameLoop`): `InstallPath=C:\ARENA\GameLoop`, `GameLoopData=C:\ARENA\GameLoopData`, `Version=7.0.19.01`, `HyperVState=1`, `VtState=1`, `ClientType=0`, `ChannelId=0xdc8e18e6`, `ExperimentId=Empty`. `InstallDir`/`Path`/`VDIImageDiscType` absent. HKLM `SOFTWARE\WOW6432Node\Tencent\GameLoop` has only `HyperVState=1`, `VtState=1`.

## 3. AppData

Root: `C:\Users\%USER%\AppData\Roaming\Tencent\GameLoop` (vendor folder `GameLoop`).

| Surface | Path / value |
|---|---|
| Master DB | `...\Tencent\GameLoop\db\Configs.db` (present; 733,184 B + 32,768 shm + 4,128,272 wal while running) |
| smk.conf (CODM) | `...\Tencent\GameLoop\config\com.activision.callofduty.shooter\smk.conf` (present, 31,325 B) |
| Other package smk | `...\config\com.proximabeta.mf.uamo\smk.conf` (present) |
| Package subfolders under `config\` | `com.activision.callofduty.shooter`, `com.proximabeta.mf.uamo` |
| DB packages in `configs` | `com.activision.callofduty.shooter`, `com.proximabeta.mf.uamo`, `com.tencent.ig`, `com.tencent.androws`, `com.tencent.launcher`, `com.android.camera2`, `com.google.android.contactkeys`, `com.google.android.safetycore` |

## 4. Registry

HKCU base `Software\Tencent\GameLoop` (same as Beta). Subkeys present: `Duration`, `Env`, `Env\GraphicInfo`, `GameLoop`, `Image`, `Mark`, `Settings`, `Settings\Common`, `Settings\GameLoop`, `Settings\GameLoopSettings`, `SDK_CloudGame`.

`Settings\Common` values (all `REG_QWORD`): `PerformanceMode=0`, `PerformanceMemoryGb=0xc`, `PerformanceCpuCoreNum=0x6`, `RenderMode=2`, `RenderUserType=0`, plus `DisableFullscreenOptimization=1`, `OutputDeviceGuid`, `InputDeviceGuid`.

`Settings\GameLoop` values: `ScreenRecordFilePath`, `AndroidSystemVolume=0x64`, `ExitPromptSwitch=1`, `BkgAppCloseBtnExit=0`, `GameMode=0`, `ShowFps=1`, `RecordAudioSwitch=1`, `AndroidAdbMode=0`, `KeyMapInputType` absent, `InstalledPkgAutoClearSwitch` absent, `CustomizedPkgSavePath` absent, `WheelScroll\*` absent, `EnterKeyMapGuidanceTimes=1`, `ShowKeymappingTips=0`.

`Settings\GameLoopSettings` values: `PreDownloadSwitch=1`, `AutoSilentUpdate=1`, `CreateAppShortcut=1`, `BossKey=0`, `AppStoreCloseOption=0`, `NotificationOption=1`, plus AI hotkey entries (`AiHotKeyOption`, `AiAssistantKey`, `AndrowsBossKey`, …).

HKLM `SOFTWARE\Tencent\GameLoop`: `Settings\GameLoopSettings` has `AndrowsAutoStartSwitch=1`, `AppStoreAutoStartSwitch=1`. `GameLoop\GameLoop` has `Version=7.0.107.0`, `SimulatorHardwareConfig`, `MachineStates`. `Env` has `svid`, `VPoDioa`, `VOPoDioa`, `VWiDioa` (values redacted), `HyperVState`-adjacent graphics info. `Image\Version=5.10.1900.91`. `Mark\*` includes `full_screen_toast_mark_com.activision.callofduty.shooter`.

Secrets present in registry (redacted, see §13): `svid`, `VPoDioa`, `VOPoDioa`, `VWiDioa`, `ams_union_id` (in HKCU `Env\basic_info`), `oaid`, `qimei`/`qimei36`, `seed_id`/`vdid` (in `sdid.conf`).

## 5. Processes & services

Captured while running (GameLoop launched by user). Process names: `GameLoop`, `GameLoopAssistant`, `GameLoopDldSvr`, `GameLoopEmulator`, `GameLoopVm`, `CefRendererProcess` (×8). `GameLoopService` process not running; service exists but status **Stopped** (Automatic). No `AppMarket`, `androidprocess`, `aow_exe`, `qmemsimulatorservice`, `sysrestart`, `txgameassistant`, `tsettingcenter` observed. New images: `GLABoxHeadless`, `GLABoxSVC`, `AceKeeper` (from `Setup.json`), `GameLoopVfs`.

Listening ports observed:
- `127.0.0.1:5283` (GameLoopEmulator)
- `0.0.0.0:5555` (GameLoopEmulator, ADB)
- `127.0.0.1:57296` / `[::1]:57296` (GameLoopDldSvr)

No scheduled task for GameLoop. Firewall rules present: GameLoop Main/Store/Store Render/Assistant/Assistant Toast/Download Service/Service/AboxHeadless (all inbound Allow). `GameLoopService` firewall rule present.

## 6. Database

`Configs.db`: SQLite, `journal_mode=delete`, `integrity_check=ok`. Tables: `configs`, `global_config`, `key_map_aux_setting`, `key_map_configs_cache_official`, `key_map_screenshot`, `sqlite_sequence`. **`migrate_status_tb` absent.**

Schema differences vs Beta:
- `configs(id INTEGER PK AUTOINCREMENT, key varchar(256) UNIQUE, value TEXT, encrypt int, json_data TEXT)` — extra columns.
- `key_map_configs_cache_official(id, key, value0, value1, value2, value3, value4)` — extra `value2..value4`.
- `key_map_screenshot(pkg_name, mode_id, solution_id, pic)`.

Triggers:
- `_cache_upd`/`_cache_ins`/`_cache_del`: guard `offcial_config2.com.activision.callofduty.shooter`, `offcial_keymap_data2...146.146`, `...531686.1063553` (raises on modify/overwrite/delete).
- `_conf_upd`/`_conf_ins`/`_conf_del`: on insert/update of `androws.com.activision.callofduty.shooter.{host_config,local_user_config,local_setting}` force FPS fields to 240; delete blocked.

Row counts: `configs=43`, `global_config=1`, `key_map_aux_setting=1`, `key_map_configs_cache_official=12`, `key_map_screenshot=2`.

CODM `configs` keys present: `androws.com.activision.callofduty.shooter.{default.key_mapping, global_config, guest_config, host_config, local_user_config, smk}`. `local_setting` absent. `androws.com.activision.callofduty.shooter.smk` present (dormant mirror).

`key_map_configs_cache_official` keys (12): `keymap_mode` ×3 (145,146,531686), `keymap_data` ×5 (145×2, 146×2, 531686×1), `lastmodeid`, `offcial_config2`, `offcial_keymap_data2` ×2 (146,531686). `recentlyused_mode.*` absent.

Mode solutions:
- 145 MP: `Multi Player` (1768790151264, 93 SwitchOperation) and `MP` (1792331370461, 0 SwitchOperation). No flat `145` solution.
- 146 BR: `Battle Royale` (146, 234 SwitchOperation) and `GameLoopHub.com` (1792331375184, 0 SwitchOperation).
- 531686 DMZ: `DMZ:禁区` (1063553, 61 SwitchOperation).

`lastmodeid.unlogin.com.activision.callofduty.shooter` = `146`. `recentlyused_mode` absent. `offcial_config2` = Switch→Texture dict, 176 `<Switch>` entries, TextureId 1..191 (172 unique). `offcial_keymap_data2` exists for 146.146 and 531686.1063553 (guarded).

`local_user_config` (CODM):
```json
{"app_close_action_set":{"close_action":0,"is_popup_inquire":true},"aspect_ratio":5,"clarity":2,"frame_rate":240,"hide_full_screen_topbar":true,"is_toolbar_open":true,"landscape_render_wnd_size":[2286,1285],"landscape_size":[-1,-1],"orientation":0,"portrait_render_wnd_size":[-1,-1],"portrait_size":[-1,-1],"resolution":[0,0],"user_has_performed_fullscreen_operation":false,"user_select_clarity":true,"fps":240,"default_frame_rate":240,"maximum_frame_rate":240,"max_fps":240,"fps_level":240,"maxFps":240,"fpsLevel":240}
```
Present perf flags: `clarity=2`, `frame_rate=240`, `user_select_clarity=true`. `low_latency_mode` and `default_frame_gen_on` absent.

`host_config` (CODM):
```json
{"tp":true,"report_notification":false,"auto_fullscreen":false,"follow_screen_aspect_ratio":false,"is_use_automatic_resolution":1,"rotation":0,"default_frame_rate":240,"maximum_frame_rate":240,"function_config":{"functionbar_config":{"items":[104,108,206,215]}},"red_point_config":{"local":[{"id":185,"function_button_id":104,"display_type":2,"source_type":1}]},"fps":240,"frame_rate":240,"max_fps":240,"fps_level":240,"maxFps":240,"fpsLevel":240}
```

Tex ids in `smk.conf`: 185 entries, `10001..10157` (142 ids) + `30013,30014,30015,30020,30022,30023,30032,30046,30047,30050,30051,30053,30068,30069,30071,30072,30073,30074,30080,30083,30084,30091,30098,30111,30124,30128,30129,30133,30134,30136,30137,30141,30142,30144,30146,30147,30148,30151,30152,30153,30154,30156,30157`. Jump-trigger ids present: 10004, 10046, 30046. **30004 absent.**

## 7. smk.conf

| Item | Value |
|---|---|
| Path | `%APPDATA%\Tencent\GameLoop\config\com.activision.callofduty.shooter\smk.conf` |
| Size | 31,325 B |
| Read-only | No (`IsReadOnly=False`) |
| Encoding | UTF-8 without BOM |
| Line endings | CRLF |
| Tex entries | 185 (`<Tex id=...>`), 185 `comm=` labels |
| `check="1"` | 8 occurrences |
| Jump-trigger Tex ids | 10004 present, 10046 present, 30004 absent, 30046 present |

## 8. Template pack (ano_tmp)

Path: `C:\ARENA\GameLoop\Application\7.0.107.0\ano_tmp` (present). Contents:
- `custom_cache\` (empty dir)
- `cache_crc.dat` (8 B)
- `cache_md5.dat` (8 B)
- `config2.dat.0955d898` (323 B)
- `tersafe.update` (4 B)

`ob_*.zip` absent; `ano_tmp_quarantine` absent.

## 9. Engine configs

Location: `C:\ARENA\GameLoopData\app\config\` (no `Component\GameLoop\` dir; it is empty).

| File | Key values |
|---|---|
| `com.activision.callofduty.shooter.conf` | `{"GPU_PSS":{"I":["ETC","ASTC"]},"multipane":{"dragBar":"false","enable":"false","orientation":"landscape","ratio":"0.5"},"softvideo":true}` |
| `com.tencent.androws.conf` | DNS `["114.114.114.114","119.29.29.29"]`; `named_filter` present; `dpiPatch=true`; `forcesoftvideo=true`; `default_render_type=9`; `required_disk_space=10`; `forceOpenGL=false`; `vattr_list` present; `minimum_required_space=5` |
| `oat_file.conf` | odex entries for CODM, PUBG, Arena Breakout, etc. |
| `sdid.conf` | `seed_id`, `vdid` present; `oaid` empty (values redacted) |
| Version dir `Androws.json` | `vmConfig.name=TVM_3000`, port 3000, `tp=true`, `glTransport=wormhole` |
| Version dir `deviceConfig.json` | `forceOpenGL=false` |
| Version dir `Config.json` | app store URL `https://pcyyb.gameloop.com/` |
| Version dir `Setup.json` | kill list includes `GLABoxHeadless.exe`, `GLABoxSVC.exe`, `AceKeeper.exe`, `AceKeeperService.exe`, `GameLoopVfs.exe`, `GameLoopVm.exe` |

## 10. Hosts

`C:\Windows\System32\drivers\etc\hosts`: no GameLoop/Tencent/Arena lines. Search of the install dir for `pckeymap` returned no matches; `yyb.qq.com` appears only in offline-page assets and logs (store host), not as a keymap download host. Therefore A5.1 host `pckeymap.yyb.qq.com` is **not referenced** in this build (ABSENT).

## 11. Assumption matrix (A1–A5 vs Beta)

| # | Beta value | Hub value | Verdict |
|---|---|---|---|
| A1.1 | vendor `Tencent\GameLoop` | `Tencent\GameLoop` | SAME |
| A1.2 | `%APPDATA%\Tencent\GameLoop\db\Configs.db` | same exact path | SAME |
| A1.3 | `%APPDATA%\Tencent\GameLoop\config\com.activision.callofduty.shooter\smk.conf` | same exact path | SAME |
| A1.4 | `<drive>\Program Files\Tencent\GameLoop` / `<drive>\Tencent\GameLoop` | `C:\ARENA\GameLoop` (not in candidate list) | DIFFERENT |
| A1.5 | `HKLM\SOFTWARE\Tencent\GameLoop` + `WOW6432Node\...\GameLoop`, values InstallDir/InstallPath/Path | `HKLM\SOFTWARE\Tencent\GameLoop\InstallPath=C:\ARENA\GameLoop`; WOW6432Node has no install value | DIFFERENT |
| A1.6 | `<root>\Application\<x.y.z>` (≥2 dots) | `Application\7.0.107.0` | SAME |
| A1.7 | `GameLoopLauncher.exe`, `GameLoop.exe`, `AppMarket.exe` | first two present; `AppMarket.exe` ABSENT; new `GLABox*`, `AceKeeper*` | DIFFERENT |
| A1.8 | `adb.exe` in version dir | present | SAME |
| A1.9 | `ano_tmp` + `ano_tmp_quarantine` in version dir | `ano_tmp` present; quarantine ABSENT | DIFFERENT |
| A1.10 | package `com.activision.callofduty.shooter` | same package present (among others) | SAME |
| A2.1 | `HKCU\Software\Tencent\GameLoop` | same | SAME |
| A2.2 | `HKCU\...\Settings\Common` | same, values present | SAME |
| A2.3 | `HKCU\...\Settings\GameLoop` | same | SAME |
| A2.4 | `PerformanceMode`, `PerformanceMemoryGb`, `PerformanceCpuCoreNum`, `RenderMode`, `RenderUserType` | all present, QWORD | SAME |
| A2.5 | `WOW6432Node\Tencent\GameLoop` has VDIImageDiscType/HyperVState/VtState | HyperVState+VtState present; VDIImageDiscType ABSENT; install branch is native 64-bit HKLM | DIFFERENT |
| A2.6 | `Settings\GameLoopSettings` + many `Settings\GameLoop` values | HKCU subkey present; HKLM has AndrowsAutoStart/AppStoreAutoStart | SAME |
| A3.1 | `key_map_configs_cache_official(key,value0,value1)` | adds `value2..value4` | DIFFERENT |
| A3.2 | `configs(key,value)` | adds `id,encrypt,json_data` | DIFFERENT |
| A3.3 | `offcial%` trigger-guarded typo | same spelling; triggers guard the same keys | SAME |
| A3.4 | `keymap_mode.unlogin.<pkg>.<mode>` | same | SAME |
| A3.5 | `keymap_data.unlogin.<pkg>.<mode>.<sol>` | same | SAME |
| A3.6 | `offcial_keymap_data2.<pkg>.<mode>.<sol>` | same (only 146, 531686) | SAME |
| A3.7 | `offcial_config2.<pkg>` | same | SAME |
| A3.8 | `lastmodeid.unlogin.<pkg>` | same (=146) | SAME |
| A3.9 | `recentlyused_mode.<pkg>` | ABSENT | ABSENT |
| A3.10 | `androws.<pkg>.smk` in `configs` | same | SAME |
| A3.11 | `androws.<pkg>.local_user_config` | same | SAME |
| A3.12 | `androws.<pkg>.host_config` | same | SAME |
| A3.13 | modes 145 (MP), 146 (BR), 531686 (DMZ) | same ids | SAME |
| A3.14 | flat sols 146 (BR), 1063553 (DMZ); 145 flat discovered | 146 + 1063553 present; MP has no flat 145; new `GameLoopHub.com` (1792331375184) in BR | DIFFERENT |
| A3.15 | perf flags clarity, low_latency_mode, default_frame_gen_on, frame_rate, user_select_clarity | clarity, frame_rate, user_select_clarity present; low_latency_mode & default_frame_gen_on absent | DIFFERENT |
| A3.16 | jump-trigger Tex 10004, 10046, 30004, 30046 | 10004, 10046, 30046 present; 30004 absent | DIFFERENT |
| A3.17 | `scenes_config={"scenes":[]}`; tables global_config, key_map_aux_setting, migrate_status_tb, key_map_screenshot | migrate_status_tb absent; scenes_config absent; other 3 present | DIFFERENT |
| A4.1 | service `GameLoopService`, exe `gameloopservice.exe` | same name/exe; Automatic; observed Stopped | SAME |
| A4.2 | "GameLoop" if image starts with `gameloop` | GameLoop* present, plus GLABox*/AceKeeper* | DIFFERENT |
| A4.3 | known images cefrendererprocess, appmarket, androidprocess, aow_exe, qmemsimulatorservice, sysrestart, txgameassistant, tsettingcenter | cefrendererprocess present; appmarket absent; others not observed | DIFFERENT |
| A4.4 | priority plan images gameloopemulator, gameloopvm, gameloop, gameloopassistant, gameloopdldsvr | all present | SAME |
| A4.5 | kill order (10 names, gameloopvm first … gamelooplauncher last) | new exes in Setup.json (GLABox*, AceKeeper*, GameLoopVfs) | DIFFERENT |
| A5.1 | mode-map host `pckeymap.yyb.qq.com` | not referenced anywhere in install/logs | ABSENT |

## 12. Tool-compatibility analysis

| # | Surface/symbol | Required change | Risk |
|---|---|---|---|
| A1.4 | `paths::install_root()` candidate list | Add discovery via registry `InstallPath` (already fallback) or add `C:\ARENA\GameLoop`-style roots; current drive scan misses it | needs-live-test |
| A1.5 | `paths::install_root_from_registry()` | Already matches native 64-bit `HKLM\SOFTWARE\Tencent\GameLoop\InstallPath`; ensure WOW6432Node not required | safe |
| A1.7 | `paths::launcher_exe()` | `GameLoopLauncher.exe`/`GameLoop.exe` still found; drop `AppMarket.exe` assumption or make optional | safe |
| A1.9 | `paths::ano_tmp_dir()` | Treat `ano_tmp_quarantine` as optional (ABSENT) | safe |
| A3.1 | `db.cpp`/`keymaps.cpp` SELECT/INSERT on official cache | Use explicit column names; extra `value2..value4` are harmless on read | safe |
| A3.2 | `configs` access | Use explicit column names; `encrypt`/`json_data` may matter for smk mirror writes | needs-live-test |
| A3.9 | `recentlyused_key()` | Handle missing key gracefully (no auto-create assumed) | needs-live-test |
| A3.14 | `fixes.cpp` mode/sol constants | 146 and 1063553 still valid; MP has no flat 145 (discover at runtime); account for new `GameLoopHub.com` solution 1792331375184 | needs-live-test |
| A3.15 | `perf.cpp` | `low_latency_mode`/`default_frame_gen_on` absent — code must not assume keys exist; clarity/frame_rate/user_select_clarity present | needs-live-test |
| A3.16 | `blind_tex_ids()` | 30004 absent; skip missing Tex ids instead of failing | safe |
| A3.17 | table references (`migrate_status_tb`, `scenes_config`) | Remove/optionalize references; migrate_status_tb ABSENT | unsafe (if hardcoded) |
| A4.2/A4.3 | `procs::is_gameloop_image()` | Add `glaboxheadless.exe`, `glaboxsvc.exe`, `acekeeper.exe`, `acekeeperservice.exe`, `gameloopvfs.exe` | needs-live-test |
| A4.5 | `procs::kKillOrder` | Update to include new images; order unverified live | needs-live-test |
| A5.1 | `hosts::kDomain` | `pckeymap.yyb.qq.com` not referenced; mode-map download host unknown/changed — do not patch blindly | unsafe |

## 13. Redactions

Masked (keys kept, values replaced with `<redacted>`): `seed_id`, `vdid`, `oaid`, `ams_union_id`, `qimei`, `qimei36`, `svid`, `VPoDioa`, `VOPoDioa`, `VWiDioa`, `guid`/`androidId` device blobs, and any 32+ hex device id. `cert_hash`, `cert_path`, `ip_latitude`, `ip_longitude`, `SyzsGuid` were not found in this install. Username in all paths normalized to `%USER%`. DB was opened read-only (`sqlite3 -readonly` / `?mode=ro`); no GameLoop files, registry, hosts, or firewall were modified.

## Appendix: command log

| Command (abbrev.) | Exit |
|---|---|
| `Get-CimInstance Win32_OperatingSystem/Processor/LogicalDisk` | 0 |
| Drive scan for `Tencent`/`GameLoop` tails | 0 |
| `reg query HKLM\SOFTWARE\Tencent /s`, `HKCU\Software\Tencent /s`, `HKLM\SOFTWARE\WOW6432Node\Tencent /s` | 0 |
| `Get-ChildItem C:\ARENA\GameLoop` (install root) | 0 |
| `sqlite3 -readonly Configs.db ".schema"` / introspection | 0 |
| `sqlite3 -readonly ... SELECT ...` (keys, counts, blobs) | 0 |
| `Get-Item smk.conf` + Tex id extraction | 0 |
| `Get-ChildItem ano_tmp` | 0 |
| `Get-Content androws.conf / sdid.conf / Setup.json` | 0 |
| `findstr /s /m pckeymap` (install dir) | 1 (no matches) |
| `findstr /s /m yyb.qq.com` (install dir) | 0 |
| `Get-Process`/`Get-Service`/`Get-NetTCPConnection` (live) | 0 |
| `Get-NetFirewallRule` (GameLoop) | 0 |
| `schtasks /query` (GameLoop filter) | 0 |
| `whoami` + admin check | 0 |

File written: `GameLoopHub-Research-Report.md` (working directory).