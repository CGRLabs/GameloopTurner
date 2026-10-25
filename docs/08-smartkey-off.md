# 08 — Smart-key auto-switch DISABLED (2026-09-20) + speed settings

## What "smart key" actually was

No on/off toggle exists anywhere (registry, Configs.db, engine JSONs — all searched). The auto-switching is structural: your active layout **DMZ:禁区 (mode 531686)** has **61 `<SwitchOperation EnableSwitch/DisableSwitch>` rules** — each key changes function by recognized screen state (door vs cable vs shop vs revive…), driven by the `smk` texture table (db id 413 + `smk.conf`, ~150 Tex templates: 开关门, 跳伞, 疾跑, 瞄准…). That's what you felt as "keymaps auto-switching".

Found while working: `lastmodeid` moved **531686 → 145 on its own** while GameLoop was running today (verified in BEFORE read). So the engine also re-points the active mode at runtime — double reason to pin a flat layout.

## What was changed (GameLoop closed first, backup in `backups/`)

| # | Where | Before → After | Effect |
|---|---|---|---|
| 1 | `key_map_configs_cache_official` → `lastmodeid.unlogin.com.activision.callofduty.shooter` | 531686 → **145** (was already 145 at flip time — runtime had moved it; pinned) | active family = flat family |
| 2 | `keymap_mode.unlogin…145` → `lastSolutionId` (+Type) | brkeymap 1789838897993 → **mpkeymap 1789838837209** | active layout = mpkeymap, **0 SwitchOperations = zero auto-switch rules**, fixed keys |
| 3 | `configs` id 433 `local_user_config` → `clarity` | 3 → **1** | lowest render tier = max FPS (image will look softer) |

Untouched, still as you had them: `frame_rate:120`, `low_latency_mode:true`, `default_frame_gen_on:false`, toolbar hidden, 916x515 render wnd. WAL checkpointed (TRUNCATE) after commit; AFTER-read verified all three.

## Follow-up 2026-09-20: clarity 1 made it LAGGIER

User-tested: clarity 1 = worse, not faster. Suspects: tier 1 may force a cheaper-but-CPU-heavy path (software upscale/scale on an already-loaded CPU) or a bad frame-pacing mode — lower render tier isn't always faster on emulator pipelines. Next: try **clarity 2** (middle tier) with GameLoop closed, same `local_user_config` edit. If 2 is also worse than 3, revert to 3 and treat clarity as sharpness-only on this engine.

## Follow-up 2 (same day): VM fed + clarity 2 + priority scripts

Applied with GameLoop closed (mid-match processes run elevated — force/graceful kill both bounce with Access denied, user quit via tray):
- `Settings\Common`: `PerformanceCpuCoreNum` 6 → **7**, `PerformanceMemoryGb` 10 → **12** (QWord, verified re-read; tier max, host keeps 1c/4GB)
- `local_user_config`: clarity 1 → **2** (verified 1 → 2 re-read; 120fps + low-latency + frame-gen OFF untouched)
- `D:\stuff\set_gameloop_priority.bat` rewritten: Emulator + Vm → High, UI → High, Assistant → Above normal, DldSvr → Below normal
- `D:\stuff\gameloop_priority.reg` rewritten for GameLoopEmulator/Vm/GameLoop.exe (needs admin merge)
- Relaunched via `Application\7.0.159.0\GameLoop.exe` (the `Application\GameLoopLauncher.exe` stub alone doesn't bring the UI up) — fresh PIDs confirmed. Now testing clarity 2.

## Follow-up 3: DMZ + Battle Royale REMOVED (auto-switch came back in lobby)

Symptom: entering CODM lobby flips layout to DMZ. Proof of runtime switching: `recentlyused_mode` showed modes 145/146/531686 all touched seconds apart. Fix: deleted the switch targets (backup `Configs.db.bak-2026-09-20-144037` in db dir + `gameloop-map\backups\`):
- Deleted: `keymap_data` + `keymap_mode` for **531686 (DMZ:禁区, 61 auto-rules)** and **146 (Battle Royale, 234 auto-rules)** — 4 rows
- Scrubbed `recentlyused_mode` → `{"145":…}` (145-only); `lastmodeid` stays 145/mpkeymap; clarity still 2
- NOT deletable: 2 `offcial_keymap_data2` blobs — DB has a delete-trigger (`Bu kritik anahtar silinemez`, "critical key") protecting official data. Inert without their mode rows, but if the engine re-downloads DMZ/BR on launch (SilentUpdate=1 on the DMZ solution), plan B is overwriting their content with flat keys instead of deleting
- Relaunched, fresh PIDs. Test: enter lobby → layout must stay mpkeymap. If DMZ/BR rows reappear, report and we go plan B.

## Follow-up 4: why delete + flat-insert weren't enough (root cause chain)

1. Deletes removed user blobs, but `offcial_keymap_data2` rows are the true content source (fallback) — protected by `_cache_upd` trigger (no UPDATE) + delete-block. Only trigger in the DB; everything else is writable.
2. Engine re-created hollow `keymap_mode.531686` (`solutions:{}`) and keeps "using" modes per `recentlyused` even with no data — plus it live-flipped mode145's solution mpkeymap → brkeymap. So it manages pointers at runtime; static pins get overridden in-session.
3. The lobby jump command = `<Switch Name="SetUp1" TextureId="4" ModeID="531686" comm="DMZ设置">` in official config (UPDATE-blocked, can't remove).
4. Flat user blob INSERT survived but was ignored → diagnosis: solution not registered in the mode row (orphan), engine fell back to official smart content.
5. Fix applied: re-registered solution 1063553 in `keymap_mode.531686` (original descriptor format) + re-pinned mode145 → mpkeymap. Both verified re-read. Pending: restart + lobby test.
6. Belt + suspenders (same restart): blinded lobby trigger texture — removed `Tex id=10095` (设置的X, DMZ-mode settings button) from user `smk` (configs 413: 185 → 184 templates) AND from `smk.conf` file (backup `smk.conf.bak-20260920`). Generic settings templates 10027/10030 kept. If 10095 was the wrong texture the blinding is harmless; if right, the SetUp1→DMZ jump never fires. Revert: restore `Configs.db.bak-20260920-144037` + `smk.conf.bak-20260920`.
7. Full assert while closed (first attempt crashed pre-commit on a wrong-table bug — nothing applied; fixed rerun clean): engine had pointed `lastmodeid` at **146 (BR)** — proof BR mode is a live jump destination too. Final state, all verified: lastmodeid 145; mode145 → mpkeymap; **both** mode531686 AND mode146 registered with **flat 0-rule user blobs** (DMZ re-flattened idempotently, BR flattened new); recentlyused 145-only; smk/file blind intact; clarity 2; registry 7/12 intact. Relaunched, fresh PIDs. Every known smart destination now serves fixed keys.
8. Big-pickle review cracked it: mapping is **Tex id = 10000 + TextureId** (10001↔DoorOpen/1, 10002↔GetIntoCar/2, 10003↔GetOutCar/3, 10014↔Run/14 — all verified). So SetUp1/TextureId=4 ↔ **Tex 10004 = 大厅邮箱 = LOBBY MAILBOX**. The 10095 blinding hit the wrong template — the real lobby trigger is the mailbox icon that's permanently on the lobby screen. Swapped live (DB + file, both verified): 10004 removed, 10095 restored from backup (185 templates). Credit big-pickle. Pending: restart + lobby test.
7. Full assert while closed (first attempt crashed pre-commit on a wrong-table bug — nothing applied; fixed rerun clean): engine had pointed `lastmodeid` at **146 (BR)** — proof BR mode is a live jump destination too. Final state, all verified: lastmodeid 145; mode145 → mpkeymap; **both** mode531686 AND mode146 registered with **flat 0-rule user blobs** (DMZ re-flattened idempotently, BR flattened new); recentlyused 145-only; smk/file blind intact; clarity 2; registry 7/12 intact. Relaunched, fresh PIDs. Every known smart destination now serves fixed keys.

Discussion reference: low-latency = smaller frame queue (keep ON, best feel-per-cost); frame-gen = fake interpolated frames + lag (keep OFF — real 120 > fake 240); clarity = render-res tier (1 = fastest); 3D rendering is and always was GPU (RX 5700 XT, GL 4.6/Vulkan wormhole) — `softvideo` is CPU decode of flat video clips (lobby/killcam) only.

## Test now

1. Launch GameLoop → start CODM. Active keymap should read **mpkeymap**.
2. Check: keys stay fixed across scenes (no per-scene remapping), image softer, 120fps feel.
3. If anything looks wrong, note exactly what (which key, which scene) — the flat mpkeymap blob (8.4KB, `keymap_data…145.1789838837209`) has the full fixed binding list to compare against.

## Revert (any one of these)

- Full: stop GameLoop (tray → Quit), copy `backups\Configs.db.bak-20260920` over `%APPDATA%\Tencent\GameLoop\db\Configs.db`, delete `Configs.db-wal`/`-shm`, relaunch. (WAL backup also in `backups\` but do NOT restore it — let it rebuild.)
- In-UI: keymap dropdown → pick DMZ:禁区 again (re-points lastmodeid to 531686); clarity slider back to 3.
