// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/sidebar.hpp — left navigation rail.
//
// Ported from legacy-python/src/ui/sidebar.py. Same seven entries, same order,
// same drift badge / dry-run / Close GameLoop / Apply All / Revert stack.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "glt/ui/app.hpp"

namespace glt::ui {

void draw_sidebar(App& app);

} // namespace glt::ui