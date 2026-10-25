# GameLoop Hub (TAC) — v1 changes

Implementation log for the Hub (TAC) variant, per `docs/targets/README.md`
(two binaries, duplicate over `#ifdef`, no auto-detection) and the ordered
"Required changes" in `docs/targets/hub/PLAN.md`.

Two build-time variants now exist:

| Variant | Macro | Core target | Output |
|---|---|---|---|
| Beta (unchanged behaviour) | `GLT_TARGET_BETA` | `glt_core` | `GameLoopTuner-Beta.exe` |
| Hub (TAC) | `GLT_TARGET_HUB` | `glt_core_hub` | `GameLoopTuner-Hub.exe` |

`GLT_TARGET_BETA` and `GLT_TARGET_HUB` are CMake options (both default `ON`, so
a plain configure builds both). The macro is `PUBLIC` on the core library so the
executable and its tests inherit exactly one define.

---

## 1. Variant selection (UI + persistence)

New shared module (compiled per variant, reads the build macro):

* `native/include/glt/core/variant.hpp`, `native/src/core/variant.cpp` —
  `variant::Id`, `compiled()` (from the build macro), `default_id()` (Beta),
  `parse()`, and persistence to `%APPDATA%\GameLoopTuner\variant.txt`
  (`load_choice` / `save_choice` / `clear_choice`, plus path-explicit forms for
  tests). A missing/garbage file falls back to Beta.

* `native/include/glt/ui/variant_picker.hpp`,
  `native/src/ui/variant_picker.cpp` — the first-launch modal selector
  ("GameLoop Beta" vs "GameLoop Hub (TAC)"). It is modal, lives until answered,
  and calls back into `App`.

* `native/include/glt/ui/app.hpp`, `native/src/ui/app.cpp` — `App` loads the
  saved choice in `startup()` and opens the picker when none exists; new API:
  `variant_picker_open()`, `choose_variant()`, `open_variant_picker()`,
  `clear_variant_choice()`. The choice is a record of the user's GameLoop; the
  **compiled** build still governs behaviour, and a mismatch is logged/toasted
  rather than silently switching paths.

* `native/src/main.cpp` — draws the picker before the confirm modal.

* `native/src/ui/page_about.cpp` — a "GameLoop variant" card showing the
  compiled build vs the saved choice, with "Change variant…" / "Clear saved
  choice".

No auto-detection: the choice is manual only, and never re-asked unless cleared.

## 2. Build-time separation

* `native/CMakeLists.txt` — added the two options, split sources into
  shared/Beta/Hub sets, added `glt_configure_variant()` and `glt_add_app()`, and
  now build one core library + one executable per enabled variant. The Beta
  library keeps the target name `glt_core` so the Beta test target is unchanged.
* `native/tests/CMakeLists.txt` — Beta suite wrapped in `if(GLT_TARGET_BETA)`
  (sources untouched); `add_subdirectory(hub)` under `if(GLT_TARGET_HUB)`.

## 3. Required Hub changes → files

Each item from `hub/PLAN.md` "Required changes", and where it lives:

| # | Change | File(s) |
|---|---|---|
| 1 | Install root from `HKLM\SOFTWARE\Tencent\GameLoop\InstallPath` first, drive scan fallback, drop `AppMarket.exe` | `paths_hub.cpp` |
| 2 | Explicit DB columns; tolerate absent `recentlyused_mode` / `migrate_status_tb` / `scenes_config` | `keymaps_hub.cpp` (+ shared `keymaps.cpp`/`db.cpp`, confirmed SAME), `fixes_hub.cpp` |
| 3 | Runtime mode/solution discovery; flat BR `GameLoopHub.com` must not be pinned as MP | `keymaps_hub.cpp` (`pick_flat_for_mode`), `fixes_hub.cpp` |
| 4 | Defensive perf-flag reads/writes (`low_latency_mode`, `default_frame_gen_on` absent) | `perf_hub.cpp`, `fixes_hub.cpp` |
| 5 | Skip absent Tex 30004 instead of failing | `fixes_hub.cpp` (DB + smk.conf halves) |
| 6 | Optional quarantine dir; `create_dirs` before move | `fixes_hub.cpp` (`quarantine`) |
| 7 | Add `GLABoxHeadless`/`GLABoxSVC`/`AceKeeper`/`AceKeeperService`/`GameLoopVfs` to identity + kill order + priority plan | `procs_hub.cpp` |
| 8 | Hosts block default-OFF in Hub fullchain | `fixes_hub.cpp` (`hub_hosts_block_default_enabled`) |
| 9 | Registry native 64-bit branch only (no WOW6432Node dependency) | `paths_hub.cpp` |
| 10 | No `migrate_status_tb` / `scenes_config` references | `fixes_hub.cpp` (`hub_absent_tables`); verified absent repo-wide |

New Hub-only headers expose the pure-logic seams the tests assert:
`paths_hub.hpp`, `procs_hub.hpp`, `perf_hub.hpp`, `keymaps_hub.hpp`,
`fixes_hub.hpp`.

### Files reused verbatim for Hub (confirmed SAME in the report)

`db.cpp`, `keymaps.cpp` (all SQL already names columns explicitly), `registry.cpp`,
`hosts.cpp`, and all shared UI/diagnostics (`app`, `report`, `drift`, `backups`,
`disk`, `journal`, `netinfo`, `strings`, `log`, pages, theme, fonts, widgets).
The report marks these surfaces SAME, so duplication would only add drift risk.

## 4. Testing

* Beta suite: untouched, still `glt_unit` (links `glt_core`).
* Hub suite: `native/tests/hub/` → `glt_unit_hub` (links `glt_core_hub`),
  covering every item in `hub/PLAN.md` "Testing plan":
  install-root contract, explicit columns, absent `recentlyused_mode`, runtime
  pin selection vs the new flat BR solution, blind-skip of 30004, hosts
  default-off, absent tables, new process images/kill order/priority table, and
  absent perf flags. DB tests build a throwaway SQLite file with the Hub column
  layout — no live install needed.

`ctest` runs both: `glt_unit` and `glt_unit_hub`.

## 5. Installer / release

* `native/installer/setup.hpp`, `native/installer/setup.cpp` — the installer now
  locates the app payload as `GameLoopTuner-Beta.exe`, `GameLoopTuner-Hub.exe`
  or the original `GameLoopTuner.exe`, and installs it as `GameLoopTuner.exe`
  (stable shortcuts/uninstall).
* `.github/workflows/release.yml` — builds, verifies and publishes both variant
  binaries (and a setup zip per variant).

## 6. Constraints honoured

* No auto-detection (manual choice only).
* No shared abstraction layer: Hub has its own paths/procs/perf/keymaps/fixes
  files; duplication accepted.
* `legacy-python/` and `scripts-ps/` untouched.
* "No hardcoded paths/versions" preserved: Hub discovers the root from the
  registry + drive scan and versions from `Application\<x.y.z>`.
* Every DB write stays transactional + backed up + verified via the existing
  `db::guarded_updates()` path.

## 7. Open items (live test — unchanged from the report)

* Exact kill order for the new images (taken from `Setup.json` as a start).
* Whether `encrypt`/`json_data` in `configs` affect smk mirror writes.
* Whether `GameLoopHub.com` should be preferred over flat `146` for BR.
* Whether the mode-map download host changed.