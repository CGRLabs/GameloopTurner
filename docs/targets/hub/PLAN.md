# GameLoop Hub (TAC) — v1 plan

## Source of truth
`docs/targets/GameLoopHub-Research-Report.md` — §11 assumption matrix and
§12 tool-compatibility analysis. Read those two sections before touching code.

## Goal
Produce a Hub (TAC) build that applies the targeted changes from the report so
the tuner works against the Arena install at `C:\ARENA\GameLoop`
(version `7.0.19.01`, build `7.0.107.0`). Behaviour identical to Beta where
the report says SAME; only the DIFFERENT/ABSENT surfaces change.

## Variant identity
- Compile-time flag: `GLT_TARGET_HUB`.
- User-facing label: "GameLoop Hub (TAC)".
- Install root in the report's test machine: `C:\ARENA\GameLoop`.

## Required changes (ordered by risk)

### 1. Install root discovery — HIGH impact, LOW risk
**Report refs:** A1.4, A1.5, §12 row 1.

Current code scans fixed drives for `<drive>\Program Files\Tencent\GameLoop` and
`<drive>\Tencent\GameLoop`. Hub installs to `C:\ARENA\GameLoop`, which is not
in that list.

- Promote `paths::install_root_from_registry()` (reads
  `HKLM\SOFTWARE\Tencent\GameLoop\InstallPath`) to the **primary** discovery
  path for the Hub build. The WOW6432Node mirror does not carry the install
  value in Hub, so do not depend on it.
- Keep the fixed-drive scan as a fallback for users who moved the install.
- Drop the `AppMarket.exe` assumption in `paths::launcher_exe()` (ABSENT in Hub);
  keep `GameLoopLauncher.exe` / `GameLoop.exe`.

### 2. Database access — explicit columns, optional keys
**Report refs:** A3.1, A3.2, A3.9, A3.17, §12 rows 5–7.

- `key_map_configs_cache_official` now has `value0..value4`. All SELECT/INSERT
  in `db.cpp` and `keymaps.cpp` must name columns explicitly (`value0`,
  `value1`) instead of `SELECT *`.
- `configs` now has `id, encrypt, json_data`. Same rule: name `key, value`
  explicitly. Investigate whether `encrypt`/`json_data` matter for the
  `androws.<pkg>.smk` mirror write (§12 row 6, `needs-live-test`).
- `recentlyused_mode.<pkg>` is ABSENT in Hub. Every read in
  `keymaps::read_recentlyused` and `fixes::scrub_recent` must tolerate a
  missing row (return nullopt / no-op) instead of throwing or auto-creating.
- `migrate_status_tb` and `scenes_config` are ABSENT. Remove or optionalize any
  reference; if any code path queries them, guard with table-existence checks.
- `key_map_screenshot` and `key_map_aux_setting` ARE present — no change.

### 3. Mode / solution discovery — runtime, not hardcoded
**Report refs:** A3.13, A3.14, §12 row 8.

- Mode IDs 145/146/531686 are SAME — keep constants.
- Flat solution IDs 146 (BR) and 1063553 (DMZ) are SAME — keep.
- MP (mode 145) has NO flat `145` solution in Hub; only "Multi Player"
  (93 rules) and "MP" (0 rules, sol `1792331370461`). `keymaps::pick_flat()`
  already discovers at runtime by rule count + name preference — verify it
  selects "MP".
- New BR flat solution `GameLoopHub.com` (sol `1792331375184`, 0 rules)
  exists. `pick_flat()` preference ladder is `mp → multi → br`; confirm it
  does not accidentally pick the new flat BR solution when pinning MP. If it
  does, tighten the preference to prefer mode-145 solutions for the pin.
- No solution IDs may be hardcoded beyond the documented constants.

### 4. Performance flags — defensive reads/writes
**Report refs:** A3.15, §12 row 9.

Hub's `local_user_config` has `clarity`, `frame_rate`, `user_select_clarity`
but NOT `low_latency_mode` or `default_frame_gen_on`.

- `perf.cpp` must not assume those keys exist. Reads return nullopt → treat as
  "not applicable" rather than "off".
- `set_latency` / `set_no_frame_gen` fixes: if the key is absent, skip writing
  it and report "not supported on this GameLoop build" instead of failing.
- Clarity and frame-rate sliders keep working.

### 5. Tex blinding — skip absent IDs
**Report refs:** A3.16, §12 row 10.

Hub has 10004, 10046, 30046 but NOT 30004.

- `fixes::blind_tex_ids()` currently returns `{10004, 10046, 30004, 30046}`.
- Change to: attempt each; if absent, log "already absent" and continue.
  The `blind_tex_in_text` function already returns false on absence — just make
  sure the callers treat false as success, not failure.

### 6. Quarantine — optional directory
**Report refs:** A1.9, §12 row 4.

- `ano_tmp` is present in Hub; `ano_tmp_quarantine` is ABSENT.
- `paths::ano_tmp_quarantine_dir()` already returns a path; `quarantine()` calls
  `str::create_dirs()` before moving. Verify create_dirs succeeds and the move
  works on a fresh machine. No code change expected, but needs live test.

### 7. Process handling — new images
**Report refs:** A4.2, A4.3, A4.5, §12 rows 11–12.

Hub has additional processes from `Setup.json`: `GLABoxHeadless.exe`,
`GLABoxSVC.exe`, `AceKeeper.exe`, `AceKeeperService.exe`, `GameLoopVfs.exe`.

- Add these to `procs::is_gameloop_image()` so they are recognised as GameLoop
  processes (for the priority table and "is GameLoop running?" checks).
- Update `procs::kKillOrder` to include the new images. Order is unverified live
  (report says `needs-live-test`) — copy the order from `Setup.json`'s kill
  list as a starting point: `GLABoxHeadless`, `GLABoxSVC`, `AceKeeper`,
  `AceKeeperService`, `GameLoopVfs`, `GameLoopVm`, …
- Keep the existing core images (`GameLoop`, `GameLoopEmulator`, `GameLoopVm`,
  `GameLoopAssistant`, `GameLoopDldSvr`, `CefRendererProcess`).

### 8. Hosts block — DO NOT apply by default
**Report refs:** A5.1, §12 row 13.

`pckeymap.yyb.qq.com` is not referenced anywhere in the Hub install. The
mode-map download host is unknown/changed in this build.

- Remove `HostsBlock` from the Hub fullchain (or make it opt-in with an
  explicit "experimental, may not apply" warning).
- Keep the fix registered so users who want it can run it, but default off.

### 9. Registry install keys — native 64-bit branch
**Report refs:** A1.5, A2.5, §12 row 2.

- Hub's install value lives in the native 64-bit
  `HKLM\SOFTWARE\Tencent\GameLoop\InstallPath`. The WOW6432Node mirror has no
  install value.
- `install_root_from_registry()` already targets the native branch — verify it
  does not require WOW6432Node to succeed.

### 10. `migrate_status_tb` / `scenes_config` references
**Report refs:** A3.17, §12 row 12.

- Search the codebase for these names. If any code path queries them, remove or
  guard with table-existence checks. (Report marks this `unsafe (if hardcoded)`.)

## Files to create (Hub variant)

Mirror the Beta tree under a variant directory, OR use compile-time flags in
the same files. The user's chosen strategy is **separate implementations even
where logic is identical**, so prefer separate files:

```
native/include/glt/core/paths_hub.hpp          (or paths.cpp with #ifdef)
native/src/core/paths_hub.cpp
native/include/glt/core/procs_hub.hpp
native/src/core/procs_hub.cpp
native/include/glt/core/perf_hub.hpp
native/src/core/perf_hub.cpp
native/tests/hub/...                           (Hub-specific tests)
```

For files that need NO Hub-specific change (`keymaps.cpp`, `db.cpp` logic,
`fixes.cpp` skeleton), the Hub build can reuse the Beta file verbatim — but
only after confirming the report's SAME verdict holds. When in doubt, duplicate.

## Testing plan (Hub)

- [ ] Install root resolves to `C:\ARENA\GameLoop` on the report's machine.
- [ ] `Configs.db` opens; `integrity_check=ok`.
- [ ] `modes()` returns 145, 146, 531686.
- [ ] `pick_flat()` selects a 0-rule solution for pinning.
- [ ] `flatten_destination` works for DMZ (1063553) and BR (146 or GameLoopHub.com).
- [ ] `blind_tex` removes 10004/10046/30046 from DB + smk.conf; skips 30004.
- [ ] `quarantine` moves ano_tmp items and sets smk.conf read-only.
- [ ] `detect_applied()` correctly reports FullChain after running it.
- [ ] Priority table includes new images.
- [ ] No crash when `recentlyused_mode` is absent.
- [ ] No crash when `low_latency_mode`/`default_frame_gen_on` are absent.

## Open questions (needs live test)

- Does `encrypt`/`json_data` in `configs` affect smk mirror writes?
- Does the engine use `recentlyused_mode` at all in Hub, or is it gone for good?
- Exact kill order for new images (use `Setup.json` as starting point).
- Whether `GameLoopHub.com` flat BR solution should be preferred over `146`.
- Whether the mode-map download host changed (do not patch blindly).
