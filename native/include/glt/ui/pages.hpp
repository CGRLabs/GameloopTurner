// ─────────────────────────────────────────────────────────────────────────────
// glt/ui/pages.hpp — the remaining page renderers.
//
// Keymaps / Performance / VM & Priority were one FixPage class in pages.py with
// a `tabname` discriminator; they are split into three functions here because
// each carries different controls and the Performance page has two extras
// (sliders, clarity selector) that the other two do not.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "glt/ui/app.hpp"

namespace glt::ui {

void draw_keymaps(App& app);
void draw_performance(App& app);
void draw_vm_priority(App& app);
void draw_network(App& app);
void draw_backups(App& app);
void draw_logs(App& app);
void draw_about(App& app);

} // namespace glt::ui