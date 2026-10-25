<p align="center">
  <img src="mythic.png" alt="GameLoop Tuner" width="128" height="128">
</p>

<h1 align="center">GameLoop Tuner</h1>

<p align="center">
  Diagnose, tune and stabilise GameLoop / CODM Mobile on any Windows PC — as a
  single native executable, with no runtime, no DLLs and no temp unpacking.
</p>

**[Download the latest release](https://github.com/realLLUKS/GameloopTurner/releases)**

---

## What it does

GameLoop switches keymap layouts **by itself**. It runs live scene detection
while the game is open: it matches image templates from `smk.conf` against what
is on screen and flips to the DMZ or Battle Royale layout when it recognises the
lobby or the skill-select box. The result is that your carefully configured
layout vanishes mid-match.

The durable fix — verified in a live session — is to:

1. **Pin** a flat (0-rule) layout as the active one.
2. **Flatten** the smart destination layouts, so any jump lands on fixed keys.
3. **Blind** the jump-trigger `<Tex>` templates in both `Configs.db` and
   `smk.conf`.
4. **Lock** `smk.conf` read-only, which is the actual kill-switch — the runtime
   template restore cannot overwrite a read-only file.
5. **Quarantine** the engine's local template pack in `ano_tmp/`.

The whole chain is one checkbox. See [`docs/keymap-auto-switch.md`](docs/keymap-auto-switch.md)
for the full investigation trail, including what was ruled out.

---

## Requirements

| | |
|---|---|
| OS | Windows 10 / 11 (x64) |
| GPU | Any DirectX 11 capable device (WARP fallback if the driver is unhappy) |
| Permissions | **Administrator** — GameLoop recreates the emulator from LocalSystem, and the close ladder must be able to terminate SYSTEM-owned processes |
| GameLoop | Installed and launched once for your account |

---

## Usage

1. Launch **GameLoopTuner.exe** and accept the UAC prompt.
2. Open the **Dashboard** for a read-only status snapshot.
3. Go to **Keymaps**, tick the amber **RECOMMENDED** card.
4. Click **Apply All**. Accept the prompt to close GameLoop.
5. Relaunch GameLoop.

Everything else on the other pages is optional and advanced.

If the sidebar ever flips to **`⚠ Drifted`**, GameLoop reverted a setting —
usually after an app update. Re-apply the recommended fix.

### Pages

| Page | What it holds |
|---|---|
| **Dashboard** | Six live cards: processes, VM allocation, active keymap, CODM settings, drive headroom, service state |
| **Keymaps** | The recommended chain, plus pin / flatten / blind / scrub / quarantine individually |
| **Performance** | Low-latency, frame-gen, VM core & RAM sliders, clarity tier |
| **VM & Priority** | Live process priorities, with a running-process table |
| **Backups** | Timestamped `Configs.db` and `smk.conf` snapshots, one-click restore |
| **Logs** | Everything that happened, with secrets stripped |
| **About** | Build info and a one-click diagnostic report |

---

## Safe by construction

* **No hardcoded paths.** Install root, `Configs.db` and `smk.conf` are
  discovered via known folders, the registry, and a scan of every fixed drive.
  No usernames, no drive letters, no GameLoop version.
* **Every write is backed up first** through SQLite's online backup API, which
  is consistent *while GameLoop is running*. Copying the file would produce a
  torn database — the WAL is routinely larger than the main file.
* **Every write is transactional and verified.** A batch either commits whole or
  rolls back, then re-reads through a fresh query to prove it stuck.
* **Secrets never leave the machine.** Device identifiers, certificate hashes and
  coordinates are dropped from the log and the diagnostic report.
* **Official keymap rows are never touched.** They are trigger-guarded by
  GameLoop; the tuner only writes `keymap_data.unlogin.*` user blobs.

---

## Build from source

Requires Visual Studio 2022 or Build Tools with the *Desktop development with
C++* workload, plus the Windows SDK and CMake ≥ 3.24.

```powershell
git clone https://github.com/realLLUKS/GameloopTurner.git
cd GameloopTurner\native
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
.\build\bin\GameLoopTuner.exe
```

> On a fresh Visual Studio 2026 install the toolchain lives at
> `C:\Program Files\Microsoft Visual Studio\18\Community`, which `vcvars64.bat`
> finds automatically. If `cmake`/`ninja` are not on `PATH`, VS bundles both
> under `Common7\IDE\CommonExtensions\Microsoft\CMake\`.

`imgui`, the SQLite amalgamation and `nlohmann/json` are fetched automatically
and pinned by version. To build offline, drop them into `native/third_party/`
and CMake will use them instead.

### Size

The release binary is **1.75 MB** and imports only system DLLs — no
`vcruntime140.dll`, no `msvcp140.dll`, nothing to install.

| Section | Size |
|---|---|
| `.text` | ~1.39 MB |
| ├─ `sqlite3.c` | ~622 KB |
| ├─ imgui (6 TUs) | ~249 KB |
| └─ this application | ~200 KB |
| `.rdata` | ~487 KB |

The port spec asked for ~1.5 MB. The remaining ~250 KB is SQLite's
virtual-table code, which cannot be removed with a define —
`SQLITE_OMIT_VIRTUALTABLE` deletes a function body but leaves its forward
declaration, and the amalgamation fails to compile with C2129. Removing it
would mean vendoring a patched amalgamation; the supported define set is
documented in `native/cmake/sqlite_flags.cmake`.

---

## Project layout

```
GameloopTurner/
├── native/                    ← the C++20 application (this is the product)
│   ├── CMakeLists.txt
│   ├── cmake/                 manifest, .rc, compiler policy
│   ├── include/glt/core/      paths db keymaps perf registry procs drift
│   │                          backups journal hosts disk log fixes report
│   ├── include/glt/ui/        theme fonts widgets app sidebar pages
│   ├── src/                   implementations + the Win32/DX11 host
│   ├── installer/             ← the setup exe (no imgui, no DX11, 180 KB)
│   └── tests/                 pure-logic unit tests
├── legacy-python/             ← the previous Python build, archived read-only
├── scripts-ps/                the PowerShell prototypes
└── docs/
    ├── keymap-auto-switch.md  how the auto-switch actually works
    └── cxx-port-audit.md      the Python → C++ port contract
```

`legacy-python/` is kept for reference. It is frozen and no longer released.

---

## Licence

Apache-2.0 — see [`LICENSE`](LICENSE) and [`NOTICE`](NOTICE).

Apache-2.0 lets you fork this and rebrand the result however you like. Section 6
withholds any trademark licence, so you may not use "Codmsurge" or "GameLoop
Tuner" to endorse a derivative. Please credit Codmsurge and link back to the
repository.