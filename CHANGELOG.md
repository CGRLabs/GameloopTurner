# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] — 2026-10-18

### Added

* **`GameLoopTunerSetup.exe` — a first-party installer, replacing Inno Setup.**
  Lives in `native/installer/` and builds as a separate CMake target on the
  same `cmake --build` as the app. It links no imgui and no DX11 — only
  Win32, GDI, Shell and Advapi32 — so it lands at 180 KB instead of the
  ~1.5 MB an off-the-shelf setup costs to ship one self-contained file.
  Installs per-machine to `%ProgramFiles%\GameLoop Tuner`, adds Start Menu
  and optional desktop shortcuts, and registers an Apps & Features entry whose
  `UninstallString` points at the copy of the setup it leaves beside the app.
  The window is hand-painted in GDI so it matches the app without inheriting
  system control theming. Self-elevates via the `runas` verb rather than
  embedding `requireAdministrator`, so a declined UAC prompt is a reportable
  failure instead of a silent one. Supports `--silent` and `--no-desktop`.

### Changed

* **Licence is now Apache-2.0** (was MIT). Apache-2.0 §6 withholds any
  trademark licence, which is what stops a fork from using "Codmsurge" or
  "GameLoop Tuner" to endorse it. Forking and rebranding the application
  itself is still unrestricted. Added `NOTICE`.
* **Rewritten as a native C++20 Windows executable.** The Python + CustomTkinter
  implementation in `legacy-python/` is archived and no longer released.
* **Single-file distribution.** A **1.75 MB** `GameLoopTuner.exe` with a
  statically linked CRT (`/MT`), so there is no `vcruntime140.dll`, no runtime
  install and nothing unpacked into `%TEMP%`. Verified: the import table
  contains only system DLLs.
* **Admin rights requested by the embedded manifest.** Windows raises the UAC
  prompt before the process image is mapped, replacing the runtime messagebox
  the Python build showed.
* **UI rebuilt on Dear ImGui (Win32 + DirectX 11).** Fixed sidebar layout,
  custom charcoal-and-amber theme, and system fonts (Segoe UI / Consolas)
  loaded from `%WINDIR%\Fonts` with per-monitor DPI awareness — replacing the
  bundled bitmap default font.
* **Idle frame throttling.** Four sleep tiers plus whole-frame skipping. Measured
  at 0.22 s of CPU across a 38 s run, with the counter flat between 10 s and
  38 s once startup finished.
* **Close ladder uses native APIs with handle synchronization.** `CreateToolhelp32Snapshot`,
  `OpenProcess`/`TerminateProcess`, the process-management service APIs and
  `AdjustTokenPrivileges(SeDebugPrivilege)` replace the `tasklist` / `taskkill`
  / `sc` shell-outs. `kill_pids` now synchronizes with `WaitForSingleObject` to
  guarantee kernel unmapping and file-lock releases before database writes.
  Expanded process matching covers `AppMarket.exe`, `cefrendererprocess.exe`,
  `androidprocess.exe`, and `aow_exe.exe`. `run_apply` automatically verifies
  and closes GameLoop before database transactions execute.
* **Options & Performance page live synchronization.** Sliders and selectors
  now display and synchronize directly with live detected registry/database values
  (e.g., VM memory in GB, CPU cores, clarity tier, latency flags). Changing them
  outside or in GameLoop automatically updates the options page with no desync or
  confusion.

### Fixed

* **The UI was drawn into ImGui's implicit `Debug` window.** Nothing ever called
  `ImGui::Begin()`, so every widget was filed into the window ImGui creates when
  it has no explicit one — which supplies its own title bar, border, rounded
  corners and collapsing header around the whole application. The UI read as a
  window inside a window. `render_frame()` now opens exactly one window pinned to
  the full client area ( `WorkPos`/`WorkSize` ) and stripped of all chrome:
  `NoTitleBar | NoResize | NoMove | NoScrollbar | NoScrollWithMouse |
  NoCollapse | NoBringToFrontOnFocus | NoSavedSettings`, with window rounding and
  border size zeroed. The theme's `WindowPadding` is left in place, since that is
  what puts pages inside their margins.
* **Dashboard cards collapsed to a few pixels.** `theme::begin_card()` opened its
  child with `ImVec2(0, 0)`, and a `BeginChild` with a zero size takes the parent's
  **remaining** height (`BeginChildEx`: `size_default.y` is `size_avail.y` unless
  `AutoResizeY` is set). On the Dashboard the card's parent is an `AutoResizeY`
  wrapper inside a table cell, and an auto-resizing window has no height yet
  while its contents are still being submitted — so the card measured its
  available height as ~0. Six cards became slivers. On other pages the same card
  sits inside a plain scroll child with a real height, so the bug was invisible
  everywhere except the page it broke. Cards now carry `AutoResizeY` and size to
  their content.
* **`PopItemWidth() too many times!`** on the Dashboard. `card()` pushed
  `PushItemWidth`, called `BeginChild`, then called `PopItemWidth` — a push lands
  on whichever window is current and `BeginChild` switches that window, so the
  pop emptied the child's stack and left the parent's push dangling for the rest
  of the frame. The push and pop now both sit inside the child.
* **The UI rendered no text at all.** Two independent defects in the font atlas,
  both invisible to the compiler and at run time, combined to produce a window
  whose cards, buttons and layout drew perfectly and whose entire text layer
  was blank.

  1. *The glyph range table had no ASCII in it.* `kGlyphRanges` carried only the
     decorative blocks (U+2000–U+27BF, U+E000–F8FF). A non-null `glyph_ranges`
     argument to `AddFontFromFileTTF()` **replaces** ImGui's default set rather
     than extending it, and `ImFontAtlas::GetGlyphRangesDefault()` is exactly
     `{ 0x0020, 0x00FF }` — Basic Latin + Latin Supplement. So no letter, digit,
     space or punctuation was ever rasterised. ImGui's fallback glyph is `?`,
     which is itself ASCII and therefore equally absent, so `ImFont::RenderText()`
     hit `if (glyph == NULL) continue;` and silently skipped every character.
     The result is not tofu boxes — it is no text.

  2. *`ImFontConfig` was memset to zero.* `base_config()` zeroed the struct
     before setting its three fields, destroying the defaults that
     `ImFontConfig::ImFontConfig()` had just installed:
     `RasterizerMultiply = 1.0f`, `RasterizerDensity = 1.0f` and
     `GlyphMaxAdvanceX = FLT_MAX`. With density at 0, `font_size = size × 0 = 0`
     and nothing rasterises; with multiply at 0, the coverage lookup table zeroes
     every glyph's alpha; with max advance at 0, `ImClamp(advance, 0, 0) == 0`
     makes every glyph zero-width so text stacks on itself. The memset also left
     `FontDataOwnedByAtlas == false`, leaking the TTF buffers. ImGui guards this
     with `IM_ASSERT(RasterizerDensity > 0.0f && "Is ImFontConfig struct
     correctly initialized?")`, which compiles out of a release build.

  The table now leads with `0x0020–0x00FF` and covers every block the UI
  actually draws, and `base_config()` default-constructs and overwrites only the
  fields it means to change. Both guards are asserted in
  `native/tests/test_fonts.cpp`, which builds a real atlas and checks that the
  glyphs are present, non-zero-width and actually have lit pixels in the texture
  — the only checks that would have caught either defect.

* **Activity log wrote blank lines.** `App::log` moved its message into the UI
  ring *before* handing the same string to the file writer, so every persisted
  entry was empty. Found by a launch smoke test, not by the unit tests.
* **A button fired on every frame.** `action_row` returns `-1` when nothing is
  pressed; two call sites compared with `== 0` and used `else` for the
  secondary action, so "nothing" was read as a click. The Backups page
  therefore prompted for a backup on every frame.
* **Build directory selection.** `app_version_dir()` compared
   `Application/*.*.*` lexically, so after a major-version bump `10.0.0.0`
  sorted before `7.0.167.0` and the wrong build was chosen. It is now a numeric
  component-wise compare.
* **`smk.conf` backups are listed.** The Backups page globbed for them in the
   `Configs.db` directory, but they are written next to `smk.conf` in a
  different tree, so the restore path was unreachable from the UI.
* **`smk.conf` writes are binary-safe.** CRLF translation is no longer possible,
  which would have broken the next `<Tex>` match over the file.
* **Journal path resolution.** A missing `Configs.db` no longer produces a
  journal in `%USERPROFILE%`; nothing is written instead.

### Added

* **Unit tests** covering the `<Tex>` matcher, the `SwitchOperation` stripper,
   the DB key builders, blob flatness counting, the version sort, the secret
   scrubber, the perf read-modify-write contract, the fixes registry, the
   registry/path/process constants, the **font atlas** (glyph range table
   coverage plus a real atlas build proving ASCII glyphs rasterise with
   non-zero advance and non-zero coverage), and the **card layout** (that a card
   inside an `AutoResizeY` parent keeps a real height, that a card inside a plain
   parent does not swallow it, and that a taller card measures taller). None of
   them require GameLoop installed.
* **Diagnostic report** export (`About` page) — one scrubbed, size-capped text
  file covering paths, processes, registry, keymaps, triggers, `smk.conf`
  integrity, disk and backups.
* **GitHub Actions release workflow** with a pinned-dependency cache,
   `SHA256SUMS.txt`, an assertion that the executable has no non-system DLL
   imports, and a manifest check.
* **`docs/cxx-port-audit.md`** — the Python → C++ port contract, including the
   parity risks found during the audit and how each was resolved.

### Notes

* The recommended fix is unchanged: pin a flat layout, flatten the DMZ and BR
  destinations, blind the jump-trigger textures in both the database and
  `smk.conf`, lock `smk.conf` read-only, and quarantine the engine's local
  template pack. See [`docs/keymap-auto-switch.md`](docs/keymap-auto-switch.md).

## [0.17.0] — Python

* Last release of the Python/CustomTkinter build, archived under
  `legacy-python/`.

[2.0.0]: https://github.com/realLLUKS/GameloopTurner/releases
[0.17.0]: https://github.com/realLLUKS/GameloopTurner/releases