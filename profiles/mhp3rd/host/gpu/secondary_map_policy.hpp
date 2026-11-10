#pragma once
#include "gpu/hud_layout.hpp"

namespace mhp3rd::gpu::secondary_map {
enum class Update { Clear, Hold, Render };
// Additional presents and temporarily missing HUD packets must not erase the
// last completed map. A real scene exit clears it, including while paused.
constexpr Update update(bool quest, bool original_frame, bool map_draws) {
    if (!quest) return Update::Clear;
    return original_frame && map_draws ? Update::Render : Update::Hold;
}
constexpr bool hide_main(bool lower_available, bool map_enabled, bool mirror, hud::Part part) {
    return lower_available && map_enabled && !mirror && part == hud::Part::Map;
}
}
