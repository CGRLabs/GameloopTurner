// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/page_dashboard.hpp — the live status board.
//
// Ported from legacy-python/src/ui/dashboard.py. SIX cards, as in the Python
// build (the port spec said four; the original layout is preserved):
//
//   GameLoop Status · VM Allocation · Active Keymap
//   CODM Settings    · Drive Health  · Service
//
// Read-only. Nothing on this page writes anything.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "glt/ui/app.hpp"

namespace glt::ui {

void draw_dashboard(App& app);

} // namespace glt::ui