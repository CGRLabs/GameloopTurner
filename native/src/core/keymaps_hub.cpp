#include "glt/core/keymaps_hub.hpp"

#include "glt/core/strings.hpp"

#include <algorithm>

namespace glt::keymaps {

std::vector<std::string_view> hub_known_mode_ids() {
    // 145 MP, 146 BR, 531686 DMZ. SAME in Hub (report A3.13).
    return {"145", "146", "531686"};
}

std::vector<std::string_view> hub_documented_flat_solutions() {
    return {"146", "1063553", kHubFlatBrSolution};
}

std::optional<Solution> pick_flat_for_mode(const std::vector<Solution>& rows,
                                           std::string_view mode, std::string_view prefer) {
    // Step 1 — 0-rule layouts inside the requested mode only. This is what
    // stops the new flat BR solution from being picked while pinning MP.
    std::vector<const Solution*> flats;
    for (const auto& r : rows) {
        if (r.mode == mode && r.rule_count == 0) flats.push_back(&r);
    }

    // Step 2 — fall back to any flat layout only when the mode has none. The
    // flatten fixes rely on this for a mode whose 0-rule blob does not exist
    // yet on a fresh install.
    if (flats.empty()) {
        for (const auto& r : rows) {
            if (r.rule_count == 0) flats.push_back(&r);
        }
    }
    if (flats.empty()) return std::nullopt;

    // Preference ladder over the solution NAME, case-insensitively. `prefer`
    // first (default "mp"), then "multi", then "br".
    const std::string_view ladder[] = {prefer, "multi", "br"};
    for (const std::string_view want : ladder) {
        if (want.empty()) continue;
        for (const auto* f : flats) {
            if (str::icontains(f->name, want)) return *f;
        }
    }
    return *flats.front();
}

} // namespace glt::keymaps