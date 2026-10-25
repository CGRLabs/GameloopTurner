# docs/targets/ — GameLoop variant support

This folder holds everything related to supporting **multiple GameLoop variants**
in the tuner. Each subfolder/file documents one target variant and the work needed
to make the tuner work against it.

## Why this folder exists

The tuner was built and validated against **GameLoop Beta**. A second variant —
**GameLoopHub (TAC)**, used in the Arena install at `C:\ARENA\GameLoop` — has
the same core database schema and keymap logic but differs in install-root
discovery, optional DB keys, missing tables, new processes, and absent
performance flags. See `GameLoopHub-Research-Report.md` for the full
assumption-matrix comparison.

## Beta-test strategy (v1)

For the first public beta we do **NOT** try to auto-detect the variant and we do
**NOT** share one code path with runtime branching. Instead:

1. **On first launch the user picks which GameLoop they have:**
   - `GameLoop Beta` (the original, validated target)
   - `GameLoop Hub (TAC)` (the Arena/multi-app build)
2. **The choice is persisted** (e.g. a small config flag / registry value / file
   in `%APPDATA%`), and the app never re-asks unless the user clears it.
3. **Each variant gets its own implementation files**, even where the logic is
   identical. Duplicate code is acceptable in v1 — correctness and isolation
   matter more than DRY. Refactoring into shared modules comes only after both
   paths are proven in the field.
4. **The variant is selected at build time where possible** (compile-time `#define
   GLT_TARGET_BETA` / `GLT_TARGET_HUB`) and at runtime only for the things that
   genuinely need it (path discovery, process lists). Prefer compile-time
   separation for anything touching SQL or process kill orders.

## Folder layout (planned)

```
docs/targets/
├── README.md                       ← this file
├── GameLoopHub-Research-Report.md   ← Hub/TAC research + assumption matrix
├── beta/                           ← Beta-specific notes / diffs
└── hub/                            ← Hub-specific notes / diffs
```

## Goals for v1 (beta test)

- [ ] Ship a Beta-only build that is byte-identical to today's behaviour.
- [ ] Ship a Hub (TAC) build that applies the targeted changes from the report.
- [ ] Add a first-launch variant selector in the UI (or a separate installer flag).
- [ ] Keep both variants' test suites independent (`tests/beta/`, `tests/hub/`).
- [ ] No auto-detection in v1 — manual selection only.
- [ ] Document every Hub-specific assumption in `hub/` so future variants can be
      added the same way.

## Non-goals for v1

- Auto-detecting the variant from the install path.
- A single shared code path with `if (variant == Hub)` branching.
- Supporting any variant other than Beta and Hub (TAC).
- Migrating existing Beta users' settings.

## Source of truth

- Beta: the current codebase (`native/`) as-is.
- Hub: `docs/targets/GameLoopHub-Research-Report.md` §12 (tool-compatibility
  analysis) + §11 (assumption matrix).
