#include "game/secondary_items.hpp"
#include <cstdlib>
#include <iostream>

using namespace mhp3rd::game;
namespace {
void require(bool value) {
    if (!value) std::abort();
}
}
int main() {
    BufferRam ram(0x08800000u, 0x03800000u);
    require(!secondary_items::read(ram));
    constexpr auto overlay = 0x0A05E600u;
    ram.store32(overlay, 0x336F574Du);
    ram.store32(overlay + 8, overlay);
    const char name[] = "game_task.ovl";
    for (unsigned i = 0; i < sizeof(name); ++i) ram.store8(overlay + 32 + i, name[i]);
    constexpr auto menu = 0x09000000u, player = 0x09100000u, table = 0x09200000u, actor = 0x09300000u;
    constexpr auto vtable = 0x09400000u;
    ram.store32(0x09FF53B0u, menu);
    ram.store32(0x08AB3640u, player);
    ram.store32(0x09FBE794u, table);
    ram.store32(table + 8, actor);
    ram.store32(actor, vtable);
    ram.store32(vtable + 0xA8u, 0x088A58DCu);
    ram.store8(player + 396166, 32);
    ram.store16(actor + 3052, 31);
    const auto pouch = player + 394158;
    ram.store16(pouch + 31 * 4, 9);
    ram.store16(pouch + 31 * 4 + 2, 10);
    ram.store8(0x089D0FA0u + 9 * 20 + 8, 24);
    ram.store8(0x089D0FA0u + 9 * 20 + 9, 2);
    auto result = secondary_items::read(ram);
    require(result && result->selected == 31 && result->capacity == 32 && result->actor == actor);
    require(result->items[31].id == 9 && result->items[31].count == 10 && result->items[31].icon == 24);
    require(result->items[0].id == 0);
    ram.store8(menu + 4, 1);
    require(!secondary_items::read(ram));
    ram.store8(menu + 4, 0);
    for (auto bad : {0u, 33u, 255u}) {
        ram.store8(player + 396166, bad);
        require(!secondary_items::read(ram));
    }
    ram.store8(player + 396166, 32);
    ram.store16(actor + 3052, 32);
    require(!secondary_items::read(ram));
    ram.store16(actor + 3052, 31);
    ram.store16(pouch, 979);
    require(!secondary_items::read(ram));
    ram.store16(pouch, 0);
    ram.store16(pouch + 2, 100);
    require(!secondary_items::read(ram));
    ram.store16(pouch + 2, 0);
    ram.store8(player + 38, 6);
    require(!secondary_items::read(ram));
    ram.store8(player + 38, 0);
    ram.store32(actor, 0xFFFFFFFCu);
    require(!secondary_items::read(ram));
    ram.store32(actor, vtable);
    ram.store16(actor + 0x60, 1);
    require(!secondary_items::read(ram));
    ram.store16(actor + 0x60, 0);
    ram.store32(0x08AB3640u, 0x0BFFFFFCu);
    require(!secondary_items::read(ram));
    std::cout << "secondary item snapshot contracts passed\n";
}
