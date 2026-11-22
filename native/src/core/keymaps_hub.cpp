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

} // namespace glt::keymaps