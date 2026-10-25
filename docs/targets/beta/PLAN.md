# GameLoop Beta — v1 plan

## Status
**Baseline / reference variant.** This is the variant the tuner was built and
validated against. No code changes are needed for v1; the existing `native/`
tree IS the Beta implementation.

## Goal
Ship a Beta build that is byte-for-byte behaviourally identical to the current
codebase, so existing users and testers see zero regression.

## What the other model should do
- Nothing functional. Treat `native/` as the Beta source of truth.
- Add a compile-time flag `GLT_TARGET_BETA` (define in CMake) so the variant
  selector and any future shared scaffolding can branch on it.
- Add the first-launch variant selector UI (see `docs/targets/README.md`),
  defaulting to "GameLoop Beta" when no choice has been persisted yet.
- Keep all existing tests under `native/tests/` as the Beta suite. Optionally
  move/copy them to `native/tests/beta/` for symmetry with `native/tests/hub/`.

## Files touched (Beta)
- `native/CMakeLists.txt` — add `GLT_TARGET_BETA` define when building Beta.
- `native/src/ui/...` (first-launch selector) — shared across variants, but
  default to Beta.
- `native/tests/` — remains the Beta suite.

## Out of scope for Beta
- Any behaviour change.
- Auto-detection.
- Shared abstraction layer.
