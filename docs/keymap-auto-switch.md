# Keymap Mode Auto-Switch — Root Cause & The Fix That Works

## TL;DR

The mode is not switched by re-downloading from the network. GameLoop does
**live, in-memory scene detection** while the game runs: it matches Tex image
templates in `smk.conf` against what's on screen and flips the active mode
(DMZ / Battle Royale / Multiplayer) based on what lobby it sees.

When the templates were missing, the engine restored them at runtime from a
**local zip packed inside the install dir** (`ano_tmp/ob_x_ver_86.zip`) — so
even blocking `pckeymap.yyb.qq.com` in the hosts file could not stop it.

**The durable kill-switch is: blind the Tex templates in `smk.conf` and mark
the file read-only.** The runtime restore cannot overwrite a read-only file,
and with no templates to match, scene detection has nothing to fire on.

## The keymap system

- `Configs.db` lives in GameLoop's appdata (see `src/core/paths.py`).
- Official blobs: `offcial_keymap_data2.<pkg>.<mode>.<sol>` — read-only,
  trigger-guarded in the DB (engine-side triggers; the tuner never writes them).
- User blobs: `keymap_data.unlogin.<pkg>.<mode>.<sol>` — writable.
- Modes: **145** = Multiplayer, **146** = Battle Royale, **531686** = DMZ.
- `lastmodeid.unlogin.<pkg>` = which mode is currently active.
- `recentlyused_mode.<pkg>` = jump history (engine uses it to "remember").
- `keymap_mode.unlogin.<pkg>.<mode>` = per-mode JSON: `lastSolutionId`,
  `lastSolutionIdType`, `solutions{...}`.
- `smk.conf` (appdata `config\<pkg>\`) and the DB row `androws.<pkg>.smk` both
  carry `<Tex id="N" ...>` templates. Tex 10004 = lobby mailbox (→ DMZ),
  Tex 10046 = skill-select box (→ BR). **The FILE is the operative source.**

## Investigation trail — what was ruled out

1. **Network re-download theory** (earlier docs): the engine allegedly fetched
   the bundle from `pckeymap.yyb.qq.com` at launch. Killed when blocking that
   host in the hosts file did NOT stop `smk.conf` from being re-burned mid-game.
2. **Database triggers**: only protect `offcial_config2` /
   `offcial_keymap_data2...` rows. Say nothing about runtime scene detection.
3. **Registry / other config files**: nothing relevant found.
4. **Live probe**: `recentlyused_mode` gained `146`/`531686` timestamps DURING
   a session and mode-145's `lastSolutionId` flipped back to `brkeymap` —
   proving the switch happens in the running emulator, in memory.

## How the switch actually works

1. `smk.conf` ships with `Tex` templates (image fingerprints).
2. While the game runs, the engine watches the screen, matches a template
   (e.g. the DMZ lobby mailbox → Tex 10004), and flips the active layout.
3. If the templates are missing, the engine re-extracts them from a **local**
   pack `ano_tmp/ob_x_ver_86.zip` (the pack also regenerates itself — it came
   back mid-session even after being moved aside, and caused no switch once
   the file was locked).
4. The DB row `androws.<pkg>.smk` is a **dormant mirror** the engine re-fills
   harmlessly — re-adding Tex rows there does NOT trigger a switch.

## What the fixes do (all local writes)

| Fix | Writes |
|-----|--------|
| `pin` | `lastmodeid` → 145, mode-145 `lastSolutionId` → flat layout |
| `flatdmz` / `flatbr` | 0-rule user blobs for DMZ / BR destinations + registers mode |
| `blind` | Removes Tex 10004 (lobby→DMZ) + Tex 10046 (skill-box→BR) from `androws.<pkg>.smk` and `smk.conf` file |
| `scrub` | `recentlyused_mode` → `{"145": 0}` only |
| `quarantine` | Moves `ano_tmp` template pack + integrity caches aside AND marks `smk.conf` read-only |
| `fullchain` | pin + flatten DMZ/BR + blind + scrub + quarantine in one apply |

## What actually stopped it (verified in a live session)

1. **Read-only `smk.conf`** — the real kill-switch. The runtime template
   restore cannot overwrite a read-only file. Verified: the pack returned
   mid-session yet the file stayed blinded and the mode never jumped.
2. **Quarantined local pack** — best-effort deprivation of restore material.
   The engine regenerates the pack, but with the file locked it can't be used.
3. **Blinded file + DB** — no Tex 10004/10046, so scene detection has nothing
   to match.
4. **Flat user blobs** — every landing (even if the engine flips modes in
   memory) is a 0-rule layout, so the visible keymap never changes.

Notably `recentlyused` and the active mode-145 solution pointer now flip
harmlessly — that is why **drift/state detection ignore pointer noise** and
gates `fullchain` only on: mode 145 active && all custom layouts flat &&
smk.conf file blinded && read-only lock present.

## Recurrence after GameLoop updates

An app update can ship a new `smk.conf` (writable, templates back). Re-run the
`fullchain` fix after any update; the tuner re-blinds and re-locks, and
`quarantine` is idempotent.

## Related files

| File | Role |
|------|------|
| `src/core/keymaps.py` | mode discovery, pin ops, flatten ops, drift report |
| `src/core/hosts.py` | block/unblock the pckeymap host (now mostly redundant) |
| `src/core/drift.py` | baseline snapshot + periodic drift check (tracks flatness + file lock) |
| `src/ui/pages.py` | fix definitions (`FIXES`) + apply functions (incl. `quarantine`) |
| `src/main.py` | `_on_drift` callback + apply-all orchestration |
| `src/ui/dashboard.py` | live keymap status card |