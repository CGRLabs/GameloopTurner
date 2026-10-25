// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/variant_picker.hpp — the first-launch "which GameLoop?" selector.
//
// Shared scaffolding (identical for both variants): it only reads App state and
// calls back into it, so it does not need a per-variant copy.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

namespace glt::ui {

class App;

// Draws the modal selector while App::variant_picker_open() is true. On choice
// it calls App::choose_variant(), which persists the answer. Does nothing when
// the user has already chosen.
void draw_variant_picker(App& app);

} // namespace glt::ui