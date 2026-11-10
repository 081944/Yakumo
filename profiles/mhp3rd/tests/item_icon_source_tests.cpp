#include "game/item_icon_source.hpp"
#include <cstdlib>
#include <iostream>
using namespace mhp3rd::game;
static void require(bool value) {
    if (!value) std::abort();
}
int main() {
    BufferRam ram(0x08800000u, 0x03800000u);
    require(!item_icons::read(ram, 24, 6));
    constexpr auto context = 0x09000000u, archive = 0x09100000u, entry = archive + 16u;
    constexpr auto palette = entry + 32u + 32768u;
    ram.store32(0x09FF1DFCu, context);
    ram.store32(context + 3200u, archive);
    ram.store32(archive, 0x484D542Eu);
    ram.store32(entry + 8u, 1u);
    ram.store32(entry + 16u, 32784u);
    ram.store32(entry + 24u, 4u);
    ram.store16(entry + 28u, 256u);
    ram.store16(entry + 30u, 256u);
    ram.store32(palette + 8u, 3u);
    ram.store16(palette + 12u, 496u);
    ram.store32(0x089FE63Cu + 24u * 4u, 22u);
    ram.store16(0x089FE758u + 6u * 4u, 12u);
    const auto rect = 0x089FEF24u + 22u * 8u;
    ram.store16(rect, 48u);
    ram.store16(rect + 2u, 48u);
    ram.store16(rect + 4u, 24u);
    ram.store16(rect + 6u, 24u);
    auto result = item_icons::read(ram, 24, 6);
    require(result && result->texels == entry + 32u && result->palette == palette + 16u + 12u * 64u);
    require(result->width == 256u && result->height == 256u && result->x == 48u && result->y == 48u);
    require(result->crop_width == 24u && result->crop_height == 24u && result->palette_format == 3u);
    require(!item_icons::read(ram, 71, 6));
    require(!item_icons::read(ram, 24, 32));
    ram.store16(rect, 240u);
    require(!item_icons::read(ram, 24, 6));
    ram.store16(rect, 48u);
    ram.store16(rect + 4u, 0u);
    require(!item_icons::read(ram, 24, 6));
    ram.store16(rect + 4u, 24u);
    ram.store32(0x089FE63Cu + 24u * 4u, 256u);
    require(!item_icons::read(ram, 24, 6));
    ram.store32(0x089FE63Cu + 24u * 4u, 22u);
    ram.store16(palette + 12u, 192u);
    require(!item_icons::read(ram, 24, 6));
    ram.store16(palette + 12u, 496u);
    ram.store32(palette + 8u, 9u);
    require(!item_icons::read(ram, 24, 6));
    ram.store32(palette + 8u, 3u);
    ram.store32(entry + 16u, 0xFFFFFFFFu);
    require(!item_icons::read(ram, 24, 6));
    ram.store32(entry + 16u, 32784u);
    ram.store32(entry + 8u, 33u);
    require(!item_icons::read(ram, 24, 6));
    ram.store32(entry + 8u, 1u);
    ram.store16(entry + 28u, 255u);
    require(!item_icons::read(ram, 24, 6));
    ram.store16(entry + 28u, 256u);
    ram.store32(archive, 0u);
    require(!item_icons::read(ram, 24, 6));
    ram.store32(context + 3200u, 0xFFFFFFFCu);
    require(!item_icons::read(ram, 24, 6));
    ram.store32(0x09FF1DFCu, 0xFFFFFFFCu);
    require(!item_icons::read(ram, 24, 6));
    std::cout << "original icon source contracts passed\n";
}
