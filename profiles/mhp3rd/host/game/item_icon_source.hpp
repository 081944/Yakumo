#pragma once
#include "game/guest_ram.hpp"
#include <optional>

namespace mhp3rd::game::item_icons {
struct Source {
    std::uint32_t texels{}, palette{}, palette_format{}, format{}, width{}, height{};
    std::uint32_t x{}, y{}, crop_width{}, crop_height{};
};
// NPJB-40001's original icon indirection, rectangle and palette tables.
std::optional<Source> read(const Ram &ram, unsigned icon, unsigned color);
}
