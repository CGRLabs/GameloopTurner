# C++20 Port — Audit & Layout Proposal

> Audit target: `src/core/` + `src/ui/` (Python/CustomTkinter) → native C++20 + Dear ImGui.
> Status: **audit complete, code not yet written.** This doc locks the port contract before any C++ exists.

---

## 1. AUDIT SUMMARY

**23 Python files, ~2,900 LOC** read in full.

| Module | LOC | Port difficulty | Notes |
|---|---|---|---|
| `core/paths.py` | 54 | Easy | `ctypes` GetLogicalDrives → direct Win32 |
| `core/db.py` | 63 | Medium | SQLite **online backup API**, not file copy |
| `core/keymaps.py` | 116 | Medium | 4 regexes + JSON mode walking |
| `core/perf.py` | 37 | Easy | JSON read/modify/write |
| `core/procs.py` | 236 | **Hard** | `tasklist`/`taskkill`/`sc` shelling → native APIs |
| `core/registry.py` | 79 | Easy | `winreg` → `Reg*Ex` |
| `core/drift.py` | 191 | Medium | thread → `std::jthread` |
| `core/backups.py` | 31 | Easy | glob → `FindFirstFileW` |
| `core/journal.py` | 26 | Easy | JSON map |
| `core/hosts.py` | 65 | Easy | line filter + write |
| `core/disk.py` | 44 | Easy | `GetDiskFreeSpaceExW` |
| `core/log.py` | 39 | Easy | rotating file + ring buffer + scrub regex |
| `core/adb.py` | 37 | Easy | `CreateProcessW` pipe (keep subprocess) |
| `core/report.py` | 87 | Low | diagnostics dump |
| `ui/theme.py` | 50 | Easy | constants → `ImGuiStyle` |
| `ui/sidebar.py` | 130 | Medium | widget tree → immediate mode |
| `ui/dashboard.py` | 296 | Medium | 6 cards + background gather thread |
| `ui/pages.py` | 967 | **Hard** | `FIXES` registry + all page UI |
| `ui/log_console.py` | 89 | Easy | ring → `ImGui::ListClipper` |
| `main.py` | 324 | Medium | Apply All orchestration + close ladder |

---

## 2. IMMUTABLE CONTRACT — MUST PORT VERBATIM

These are the reverse-engineered GameLoop `androws` specifics. **Any drift here breaks the fix silently.**

### 2.1 Identity
```cpp
constexpr std::wstring_view PKG = L"com.activision.callofduty.shooter";
```

### 2.2 Modes & known solution IDs
| Mode | Meaning | Flat solution ID | Name |
|---|---|---|---|
| `145` | Multiplayer (works for all modes) | `1792013098464` | `mpkeymap` |
| `146` | Battle Royale | `146` | `Battle Royale` |
| `531686` | DMZ (搜打撤) | `1063553` | `DMZ` |

Mode-145 also contains `1792013096641` (`brkeymap`, non-flat) and `1768790151264` (`Multi Player`, 93 rules — **must be flattened too**, per `fix_fullchain`).

### 2.3 SQL — tables + key patterns (verbatim)
```sql
-- tables
key_map_configs_cache_official          -- (key, value0, value1)
configs                                 -- (key, value)

-- key_map_configs_cache_official
keymap_mode.unlogin.<pkg>.<mode>        -- JSON
keymap_data.unlogin.<pkg>.<mode>.<sol>  -- user blob XML (writable)
offcial_keymap_data2.<pkg>.<mode>.<sol> -- official blob XML (trigger-guarded, READ-ONLY)
offcial_config2.<pkg>                   -- value1 holds <Switch ModeID="N"> jump table
lastmodeid.unlogin.<pkg>                -- "145"
recentlyused_mode.<pkg>                 -- JSON {"145": 0}

-- configs
androws.<pkg>.smk                       -- DB mirror of smk.conf (dormant)
androws.<pkg>.local_user_config         -- JSON: clarity, low_latency_mode, ...
androws.<pkg>.host_config               -- JSON: default_frame_rate, maximum_frame_rate

-- discovery SQL (both tables)
SELECT key, value0 FROM key_map_configs_cache_official WHERE key LIKE 'keymap_mode.unlogin.<pkg>.%'
SELECT key, value0 FROM key_map_configs_cache_official WHERE key='keymap_data.unlogin.<pkg>.<mode>.<sol>'
SELECT value  FROM configs WHERE key='androws.<pkg>.smk'
SELECT value  FROM configs WHERE key='androws.<pkg>.local_user_config'
SELECT value  FROM configs WHERE key='androws.<pkg>.host_config'
SELECT value1 FROM key_map_configs_cache_official WHERE key='offcial_config2.<pkg>'
SELECT name, tbl_name, sql FROM sqlite_master WHERE type='trigger'
SELECT key FROM key_map_configs_cache_official WHERE key LIKE 'offcial%'   -- guarded_keys probe

PRAGMA wal_checkpoint(TRUNCATE)          -- after every guarded write
```

### 2.4 Regexes (verbatim — semantics depend on `.` matching newline)
```cpp
// blind one Tex template (NOTE [^>]* spans newlines in Python — must match that)
R"(<Tex id="%d"[^>]*\n?\s*comm="([^"]*)"\s*/>)"

// jump tags from offcial_config2 value1
R"(<Switch [^>]*ModeID="(\d+)"[^>]*>)"
R"(<Switch [^>]*>)"

// enumerate Tex ids in smk.conf file
R"(<Tex id="(\d+)")"

// flatten: strip all SwitchOperations (both forms)
R"(<SwitchOperation\b[^>]*/>)"
R"(<SwitchOperation\b[^>]*>.*?</SwitchOperation>)"   // needs DOTALL

// log scrub (secrets)
R"(("?)(seed_id|vdid|oaid|ams_union_id|cert_hash|VPoDioa|VOPoDioa|VWiDioa)\1\s*[:=]\s*"[^",\s}\]]+")"

// flatness test: a layout is "flat" iff blob.count("SwitchOperation") == 0
```

> ⚠️ C++ `std::regex` does **not** support `\b` + DOTALL semantics identically and is slow. Use a tiny hand-rolled scanner or bring in a small regex lib. Recommended: hand-written matchers for the four keymap patterns (they're simple), `std::regex` only for the scrub pattern.

### 2.5 Blind Tex IDs
```cpp
constexpr int BLIND_TEX_IDS[] = { 10004, 10046 };
// + 20000 twins → 30004, 30046   (blinding 10046 alone left BR firing — both series must go)
```

### 2.6 Registry
```cpp
HKEY_CURRENT_USER
  \Software\Tencent\GameLoop                 // BASE
  \Software\Tencent\GameLoop\Settings\Common // COMMON  ← values live HERE, not at BASE
```
Values (all `REG_QWORD` on write):
| Name | Cap / meaning |
|---|---|
| `PerformanceCpuCoreNum` | ≤ host_cores − 1 |
| `PerformanceMemoryGb` | ≤ host_mem_gb − 2 |
| `PerformanceMode` | read-only display |
| `RenderMode` | 0=Auto, 1=OpenGL+, 2=DirectX+, 3=Vulkan — **display only, never written** |
| `RenderUserType` | 0=never overridden, 1=manual — **display only** |

### 2.7 Process / service
```cpp
constexpr std::wstring_view SERVICE = L"GameLoopService";
// Force-kill order: game first, UI last. Service never in this list.
KILL_ORDER = { gameloopvm.exe, gameloopemulator.exe, gameloopassistant.exe,
               gameloopassistanttoast.exe, gameloopdldsvr.exe, gameloopvfs.exe,
               gameloopupdate.exe, gameloopinstaller.exe, cefrendererprocess.exe,
               gameloop.exe, gamelooplauncher.exe };
PRIO = { high=0x80, above=0x8000, normal=0x20, below=0x4000 };
```

### 2.8 Quarantine patterns
```cpp
QUARANTINE_PATTERNS = { "ob_*.zip", "config2.dat.*", "cache_crc.dat", "cache_md5.dat" };
// dest: <Application\<ver>\ano_tmp_quarantine>
// + SetFileAttributesW(smk.conf, FILE_ATTRIBUTE_READONLY)  ← the durable kill-switch
```

### 2.9 Hosts file
```cpp
HOSTS = <GetWindowsDirectoryW>\System32\drivers\etc\hosts
DOMAIN = "pckeymap.yyb.qq.com"
MARK   = "# gameloop-tuner: block mode-map download"
```

### 2.10 Drift TRACKED_KEYS (authoritative — from `drift.py`)
```
cores, mem_gb, lastmodeid, flat_145_all, flat_146, flat_531686,
clarity, low_latency_mode, default_frame_gen_on, file_tex_clean, smk_readonly
```
Labels map in `drift.py::LABELS`. Format: `"%s changed: was %s → now %s"`.

### 2.11 Misc constants
```cpp
ENGINE_MIN_FREE_GB = 10.0
SIDEBAR_W = 220;  CARD_PAD = 16;  CARD_RADIUS = 16;  PAGE_PAD_X = 20;  PAGE_PAD_Y = 16;
journal file = "tuner-journal.json"   // .json, NOT .jsonl
backup pattern = "Configs.db.bak-YYYYMMDD-HHMMSS", "smk.conf.bak-YYYYMMDD-HHMMSS"
```

---

## 3. PROPOSED C++ FILE LAYOUT

### Repo restructure
```
GameloopTurner/
├── native/                    ← NEW: all C++ lives here
├── legacy-python/             ← MOVED: current src/ + PyInstaller specs archived
├── scripts-ps/                ← unchanged
├── docs/
│   ├── keymap-auto-switch.md
│   └── cxx-port-audit.md      ← THIS FILE
├── .github/workflows/release.yml  ← rewritten for CMake+MSVC
├── LICENSE, README.md, CHANGELOG.md, CONTRIBUTING.md
└── pyproject.toml             ← kept for historical reference
```

### `native/` tree

```
native/
├── CMakeLists.txt
├── CMakePresets.json                 # Release/Debug + /MT / LTCG presets
├── cmake/
│   ├── app.manifest.in               # requireAdministrator
│   ├── resources.rc                  # icon + manifest embed
│   ├── sqlite_flags.cmake            # SQLITE_OMIT_* for size
│   └── warnings_msvc.cmake
├── third_party/
│   ├── imgui/                        # FetchContent pinned tag
│   ├── sqlite3/  { sqlite3.c, sqlite3.h }
│   └── json/     { nlohmann/json.hpp }
├── include/glt/
│   ├── core/
│   │   ├── strings.hpp        # utf8<->wide, split, join, trim, timestamp
│   │   ├── paths.hpp          #  54 LOC
│   │   ├── db.hpp             #  63
│   │   ├── keymaps.hpp        # 116   (modes, solutions, pin, flatten, blind, drift_report)
│   │   ├── perf.hpp           #  37
│   │   ├── registry.hpp       #  79
│   │   ├── procs.hpp          # 236   (largest: close ladder + priorities + host resources)
│   │   ├── drift.hpp          # 191
│   │   ├── backups.hpp        #  31
│   │   ├── journal.hpp        #  26
│   │   ├── hosts.hpp          #  65
│   │   ├── disk.hpp           #  44
│   │   ├── log.hpp            #  39
│   │   ├── report.hpp         #  87
│   │   └── fixes.hpp          #  (NEW) the FIXES registry, typed enum keys
│   └── ui/
│       ├── theme.hpp          #   50
│       ├── fonts.hpp          # (NEW) Segoe UI + Consolas loading, glyph ranges
│       ├── widgets.hpp        # (NEW) StatusCard / ProgressBar / HeroCard / Expander
│       ├── app.hpp            # (NEW) ApplyAll orchestration + pending queue
│       ├── sidebar.hpp        #  130
│       ├── page_dashboard.hpp #  296
│       ├── page_keymaps.hpp   # (split from pages.py)
│       ├── page_performance.hpp
│       ├── page_vm_priority.hpp
│       ├── page_backups.hpp   # (split from pages.py)
│       ├── page_logs.hpp      #   89 (log_console.py)
│       ├── page_about.hpp
│       └── modal.hpp          # (NEW) close-consent / message boxes
├── src/
│   ├── main.cpp               #  324 → WinMain + DX11 + loop + idle throttle
│   ├── core/*.cpp             # 1:1 with headers above
│   └── ui/*.cpp
└── tests/                     # (NEW) pure-logic unit tests, no GameLoop needed
```

### Module → file mapping (1:1, no mergers)

| Python | C++ | Port shape |
|---|---|---|
| `paths.py` | `core/paths.{hpp,cpp}` | free functions in `glt::paths` |
| `db.py` | `core/db.{hpp,cpp}` | RAII `Db` wrapper (move-only), `GuardedUpdateResult` |
| `keymaps.py` | `core/keymaps.{hpp,cpp}` | free fns + `struct Solution{Mode,Sid,Name,RuleCount}` |
| `perf.py` | `core/perf.{hpp,cpp}` | nlohmann json helpers |
| `registry.py` | `core/registry.{hpp,cpp}` | free fns in `glt::reg` |
| `procs.py` | `core/procs.{hpp,cpp}` | `struct ProcInfo`, ladder returns `CloseResult` |
| `drift.py` | `core/drift.{hpp,cpp}` | `DriftMonitor` w/ `std::jthread` + `std::stop_token` |
| `backups.py` | `core/backups.{hpp,cpp}` | free fns |
| `journal.py` | `core/journal.{hpp,cpp}` | free fns over nlohmann |
| `hosts.py` | `core/hosts.{hpp,cpp}` | free fns |
| `disk.py` | `core/disk.{hpp,cpp}` | free fns |
| `log.py` | `core/log.{hpp,cpp}` | singleton logger + ring buffer |
| `report.py` | `core/report.{hpp,cpp}` | free fn |
| `theme.py` | `ui/theme.{hpp,cpp}` | `void apply_dark_theme();` + color constants |
| `sidebar.py` | `ui/sidebar.{hpp,cpp}` | `void draw_sidebar(App&);` |
| `dashboard.py` | `ui/page_dashboard.{hpp,cpp}` | `void draw_dashboard(App&);` |
| `pages.py` | `ui/page_keymaps/performance/vm_priority/backups.{hpp,cpp}` | **split 1 → 4** |
| `log_console.py` | `ui/page_logs.{hpp,cpp}` | |
| `main.py` | `src/main.cpp` + `ui/app.{hpp,cpp}` | `App` class extracted for testability |
| — | `core/fixes.{hpp,cpp}` | typed `FixKey` enum + `FixDef` table (replaces `FIXES` dict) |
| — | `ui/fonts.{hpp,cpp}` | DPI-aware font atlas |
| — | `ui/widgets.{hpp,cpp}` | reusable ImGui card/bar primitives |
| — | `ui/modal.{hpp,cpp}` | ImGui popup wrappers |

---

## 4. PARITY RISKS — READ BEFORE WRITING CODE

### 🔴 HIGH — will silently break the fix

**R1. `[^>]*` spans newlines in Python regex; `std::regex` treats `.` differently but `[^>]` also spans newline — however `\s*` and the multi-line `comm="…"` layout differ.** The real `smk.conf` has entries like:
```
<Tex id="10001" width="1024" ... path="225154410"
	comm="开关门"/>
```
i.e. `comm` on the **next line**. Hand-write the matcher; do not trust `std::regex`.

**R2. `db.backup()` uses the SQLite online backup API** (`sqlite3_backup_init/step/finish`) so the copy is consistent **while GameLoop runs**. A raw `CopyFileW` produces a torn DB. Must port to the API, keep `CopyFileW` only as fallback.

**R3. `taskkill /T` kills the whole process tree.** Native `TerminateProcess` does not. Must walk `PROCESSENTRY32.th32csParentProcessID` recursively or the VM child survives and the ladder reports "still running" forever.

**R4. Read-only smk.conf is the *only* durable kill-switch.** If `SetFileAttributesW` is forgotten, or if the file is rewritten while RO is set (Python clears RO first, then writes, then re-sets — `pages.py::_blind_smk_file` does `_set_smk_readonly(False)` *before* write), the engine re-burns templates and the auto-switch returns. Port that unlock→write→relock sequence verbatim.

**R5. `json.dumps(separators=(',',':'))` compact form.** Python writes `{"clarity":2}`. `nlohmann::json::dump()` also emits `{"clarity":2}` — but `dump(-1, ' ', false)` defaults are fine; do **not** call `.dump(4)`. Also note nlohmann's object is `std::map` → **keys serialize alphabetically**, not insertion-ordered. Functionally fine, cosmetically different from Python output.

**R6. `newline=''` on every text write.** Python writes `smk.conf` and `hosts` with `newline=''` — meaning **no CRLF translation**. In C++ open with `std::ios::binary` or you will inject `\r\n` into XML and break the next regex pass.

### 🟡 MEDIUM

**R7. Icon glyphs.** `NAV_ITEMS` uses `⬡ ⌨ ⚡ ⚙ 💾 ≡ ℹ` and log icons `✓ ✗ ⚠ ● ◌ ⇄`. **Segoe UI does not contain these.** Need `Segoe UI Symbol` / `Segoe MDL2 Assets` / `Segoe UI Emoji` as a second merged font, or swap to ASCII/BMP-safe glyphs. Verify before freezing the atlas or you get tofu boxes.

**R8. `tasklist /FO CSV /NH` parsing → native enumeration.** Python filters `name.lower().startswith('gameloop')` and `'tuner' not in name.lower()`. Toolhelp32 gives image name directly — keep both filters, and keep `mem_kb` populated (Toolhelp32 doesn't give it; use `GetProcessMemoryInfo` from `psapi.lib`).

**R9. `sc query` / `sc stop` shelling → `OpenSCManagerW`.** Python shells out (`sc.exe`); C++ should use the SCM API. Note the Python version *returns the raw `sc` line* as `state` (e.g. `STATE : 4 RUNNING`) and the dashboard does `'running' in str(state).lower()`. Native returns a numeric `SERVICE_STATUS.dwCurrentState`. Either reproduce the string or update `is_running()` — **pick one and keep the dashboard consistent**.

**R10. `_enable_debug_privilege()`** (`SeDebugPrivilege` via `AdjustTokenPrivileges`) is required to kill SYSTEM/elevated GameLoop mid-match. Must port; without it the ladder stalls at stage 2.

**R11. `app_version_dir()` sorts `Application/*.*.*` with `sorted()` = lexical, then takes `[-1]`.** e.g. `"7.0.167.0"` vs `"10.0.0.0"` → lexical puts `10.0.0.0` first, `[-1]` returns the *wrong* (older-looking) dir. This bug exists in Python; porting it faithfully preserves the bug. **Decision needed:** fix (numeric sort) or keep?

**R12. Threads.** Python: `threading.Thread(daemon=True)` for drift, dashboard gather, and the whole Apply All work function. C++: `std::jthread`. **ImGui is not thread-safe** → all UI mutation must marshal through a queue drained on the main thread (Python used `self.after(0, …)`; C++ needs an explicit `UiQueue`).

### 🟢 LOW
- **R13** `/O2` and `/Os` conflict (§5). Pick one.
- **R14** UAC: Python shows a `messagebox` and exits. C++ manifest makes Windows prompt before launch — replace the check with an assert + fallback dialog.
- **R15** `log.py` ring buffer is capped at 500 lines (`del _mem[:-500]`) — keep the cap.
- **R16** `detect_applied()` pre-checks fix checkboxes at startup — preserve or the UI won't reflect reality.

---

## 5. BUILD SYSTEM NOTES (corrections to the spec)

**Size budget reality check.** Target ~1.5 MB is achievable but needs work:

| Component | Raw | With flags |
|---|---|---|
| sqlite3.c | ~1.3 MB obj | **~600 KB** — requires aggressive `SQLITE_OMIT_*` |
| imgui (6 TUs) | ~900 KB obj | ~300 KB (`/O1`) |
| our code | ~250 KB | ~150 KB |
| **total** | | **~1.0–1.4 MB** ✅ |

Required SQLite defines for size:
```cmake
SQLITE_OMIT_LOAD_EXTENSION
SQLITE_OMIT_DEPRECATED
SQLITE_OMIT_SHARED_CACHE
SQLITE_OMIT_PROGRESS_CALLBACK
SQLITE_OMIT_COMPLETE
SQLITE_DQS=0
SQLITE_THREADSAFE=0        # we guard with our own mutex (single-connection app)
SQLITE_DEFAULT_MEMSTATUS=0
SQLITE_DEFAULT_FOREIGN_KEYS=0
SQLITE_LIKE_DOESNT_MATCH_BLOBS
SQLITE_USE_ALLOCA
SQLITE_ENABLE_MATH_FUNCTIONS # keep? (drift.py doesn't need math — consider omitting)
SQLITE_OMIT_EXPLAIN
```

**`/O2` vs `/Os`:** `/Os` is an alias for `/O1`. Specifying both is contradictory (last one wins, with warning `D9025`). Recommend dropping `/O2` and using `/O1` (size-optimized) — that is what `/Os` means anyway.

**Recommended MSVC flags:**
```
/MT /O1 /GL /LTCG /Gy /Gw /DNDEBUG /GS- /GR- /EHsc
/Zc:__cplusplus /Zc:inline /utf-8
/subsystem:windows /ENTRY:wWinMainCRTStartup
/LTCG /link /LTCG /OPT:REF /OPT:ICF
```

**SQLite threading caveat:** `SQLITE_THREADSAFE=0` means one connection at a time. Drift thread + Apply All thread both touch the DB → wrap `Db` in a `std::recursive_mutex` shared process-wide, or use `SQLITE_THREADSAFE=1` (serialized) and skip the mutex. **Recommend `SQLITE_THREADSAFE=1`** — simpler, ~40 KB cost.

**Manifest:**
```xml
<requestedExecutionLevel level="requireAdministrator" uiAccess="false"/>
```

---

## 6. OPEN DECISIONS (need your call before code)

| # | Question | My recommendation |
|---|---|---|
| Q1 | Repo layout: `native/` + move Python to `legacy-python/`, or root replacement? | `native/` + `legacy-python/` — keeps scripts-ps/docs intact |
| Q2 | `app_version_dir()` lexical-sort bug — port faithfully or fix? | **Fix** (numeric-aware compare) — it's a latent update-time failure |
| Q3 | `/O2` vs `/Os` for the 1.5 MB target? | `/O1` (=`/Os`), drop `/O2` |
| Q4 | `SQLITE_THREADSAFE=0` + own mutex, or `=1`? | `=1` (serialized) — less code, safer |
| Q5 | Service state: reproduce Python's raw `sc` string, or use numeric SCM state? | Numeric + update dashboard check |
| Q6 | Icon glyphs: add Segoe UI Symbol/MDL2 font, or replace with BMP-safe icons? | MDL2 merge (keeps the look) |
| Q7 | `src/legacy` — do you want the Python app still runnable, or archived read-only? | Archived read-only (Python frozen as-is) |
| Q8 | Release artifact: 1 `.exe` only, or `.exe` + installer? | `.exe` + installer. Originally Inno Setup (`installer.iss`), which was never buildable in CI because `iscc` is not on the runner. Superseded: `native/installer/` is now an in-tree CMake target (`GLTSetup`), so the setup ships from the same `cmake --build` as the app. |

---

## 7. NEXT STEP

Once Q1–Q8 are answered, generation order:

1. `CMakeLists.txt` + `cmake/` + manifest + `main.cpp` skeleton → **builds & shows an empty DX11 window** (proves toolchain)
2. `core/strings`, `core/log`, `core/paths` → port #1 (no deps)
3. `core/db` (+ SQLite) → port #2 (proves backup API)
4. `core/keymaps` + `core/perf` → port #3 (the actual fix logic)
5. `core/registry` + `core/procs` + `core/hosts` + `core/backups` + `core/journal` + `core/disk`
6. `core/drift` + `core/fixes`
7. `ui/theme` + `ui/fonts` + `ui/widgets` → first visible cards
8. `ui/sidebar` + `ui/app` + Apply All orchestration
9. `ui/page_*` (dashboard, keymaps, performance, vm_priority, backups, logs, about)
10. `tests/` + `.github/workflows/release.yml`