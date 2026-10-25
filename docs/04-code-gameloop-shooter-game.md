# 04 — Code "game loop" in shooter-game/game.js (NOT the emulator)

File: `D:\Projects\forfun\shooter-game\game.js:706-796`. Entry `index.html` loads it; boot calls `resize(); reset(); updateCamera(); requestAnimationFrame(loop)`.

## The loop (simplified)

```
loop(t):
  requestAnimationFrame(loop)   # re-arm first — one bad frame can't kill rAF (guarded try/catch + LOG.error with frame context)
  dt = (t - last)/1000; last = t
  if !(0 < dt < 0.1): dt = 0.016   # clamp tab-switch spikes; 60Hz fallback
  LOG.frame(dt); every 0.5s LOG.stats({fps, enemies, bullets, ebullets})
  simDt = dt
    if hitStopT>0: simDt = dt*0.05; hitStopT -= dt     # blade-kill freeze (~40ms)
    simDt *= Game.timeScale                             # boss-death slow-mo
  worldDt = slowmo ? simDt*0.25 : simDt                # Y slow-mo: world crawls
  playerDt = simDt                                     # player stays 1x
  if !paused && !gameover: update(worldDt, playerDt)
  render()
  FX.tick(ctx, worldDt)  # particles in world-time; camera translate handled (UI.draw owns transform if present)
```

## Why it matters here

- Frame-rate independent: all movement × dt (see `read.txt:10` rule; `wallfx.js:update(dt)` clamps to 0.05; `weapons.js:tick(dt)` guards `0<dt<0.25` else 0.016).
- Split timesteps (world vs player) let slow-mo / hit-stop / timeScale compose without touching update logic.
- Camera: follows player, clamped to WORLD (2400x1800), DPR capped at 2 (`game.js:13-29`).
- Perf hooks: `debug.js:LOG.frame/stats` → EMA fps + dt avg; `perf.js` auto-degrade watches the 0.5s stats; `REVIEW-alice.md` notes the O(n²) separation hot loop (`game.js:419` AABB reject before hypot) + FX-ticks-while-paused low-prio issue (`game.js:371`).

One-liner: the emulator (01/02) runs your CODM; this loop runs your browser prototype — same word, unrelated systems.
