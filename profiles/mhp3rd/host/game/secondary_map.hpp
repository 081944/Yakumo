#pragma once
#include <cstdint>

namespace psprecomp { class Runtime; }
namespace mhp3rd::game::secondary_map {
// Only the map painter's inventory query may bypass possession. Other callers
// retain the real quantity, including inventory UI, rewards and saving.
constexpr bool reveal(bool lower, bool enabled, bool quest, bool supported,
    std::uint32_t return_address, std::uint32_t item) {
    return lower && enabled && quest && supported && return_address == 0x0A16657Cu && item == 670u;
}
void frame(psprecomp::Runtime &runtime);
}
