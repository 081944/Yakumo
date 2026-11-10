#pragma once

#include "game/guest_ram.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace psprecomp { class Runtime; struct AllegrexContext; }
namespace mhp3rd::game::secondary_items {
struct Item { std::uint16_t id{}, count{}; std::uint8_t icon{}, color{}; };
struct Snapshot {
    std::uint32_t actor{};
    unsigned selected{}, capacity{};
    std::array<Item, 32> items{};
};
std::optional<Snapshot> read(const Ram &ram);
// Called at the end of the display import. True when it scheduled a guest call.
bool frame(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx);
}
