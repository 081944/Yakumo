#include "game/secondary_items.hpp"
#include "game/control_context.hpp"
#include "game/game_data.hpp"

#if defined(MHP3RD_ANDROID_APP)
#include "kernel/kernel.hpp"
#include "platform/android_secondary_screen.hpp"
#include "psprecomp/runtime.hpp"
#include <iostream>
#include <cstdlib>
#endif

namespace mhp3rd::game::secondary_items {
namespace {
bool mapped(const Ram &ram, std::uint32_t at, std::size_t size) {
    return at >= 0x08800000u && at < 0x0C000000u && !(at & 3u) &&
        size <= 0x0C000000u - at && ram.contains(at, size);
}
}
std::optional<Snapshot> read(const Ram &ram) {
    const auto context = control_context::read(ram);
    if (context.scene != control_context::Scene::Quest || context.interface_open ||
        !ram.contains(0x08AB3640u, 4u) || !ram.contains(0x09FBE794u, 4u)) return std::nullopt;
    const auto player = ram.load32(0x08AB3640u);
    const auto table = ram.load32(0x09FBE794u);
    if (!mapped(ram, player, 396167u) || !mapped(ram, table, 32u)) return std::nullopt;
    const auto local = ram.load8(player + 38u);
    if (local >= 6u) return std::nullopt;
    const auto actor = ram.load32(table + 8u + 4u * local);
    if (!mapped(ram, actor, 0x14B4u)) return std::nullopt;
    const auto vtable = ram.load32(actor);
    if (!mapped(ram, vtable, 0xACu) || ram.load32(vtable + 0xA8u) != 0x088A58DCu ||
        ram.load16(actor + 0x60u) != local) return std::nullopt;
    Snapshot result;
    result.actor = actor;
    result.capacity = ram.load8(player + 396166u);
    result.selected = ram.load16(actor + 3052u);
    if (!result.capacity || result.capacity > result.items.size() || result.selected >= result.capacity)
        return std::nullopt;
    // NPJB-40001: 0x0886DD5C returns player + 394158 + 4 * slot.
    for (unsigned i = 0; i < result.capacity; ++i) {
        const auto at = player + 394158u + 4u * i;
        auto &item = result.items[i];
        item.id = ram.load16(at);
        item.count = ram.load16(at + 2u);
        if (item.id >= 979u || item.count > 99u) return std::nullopt;
        const auto info = 0x089D0FA0u + 20u * item.id;
        if (!ram.contains(info, 20u)) return std::nullopt;
        item.icon = ram.load8(info + 8u);
        item.color = ram.load8(info + 9u);
    }
    return result;
}

#if defined(MHP3RD_ANDROID_APP)
bool frame(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    static const bool disabled = std::getenv("MHP3RD_NO_SECONDARY_ITEMS") != nullptr;
    if (disabled) return false;
    GuestRam ram(runtime.memory());
    const auto snapshot = read(ram);
    android::publish_second_screen_items(ram, snapshot);
    android::publish_second_screen_icons(runtime.memory(), snapshot);
    const auto request = android::take_second_screen_item_request();
    if (!request || !snapshot) return false;
    const auto actor = snapshot->actor;
    // Check the supported executable and quest overlay before guest calls.
    if (ram.load32(0x088AB164u) != 0x27BDFFE0u ||
        ram.load32(0x0A108BB0u) != 0x27BDFF90u ||
        ram.load32(0x0A0F72C8u) != 0x8C820000u) return false;
    if (*request == -1 || *request == 1) {
        kernel().call_guest(ctx, 0x088AB164u,
            {actor, snapshot->selected, *request < 0 ? 1u : 0u, 0u},
            [actor, direction = *request](auto &returned, std::uint32_t slot) {
                GuestRam current(kernel().runtime().memory());
                const auto now = read(current);
                if (now && now->actor == actor && slot < now->capacity) {
                    current.store16(actor + 3052u, static_cast<std::uint16_t>(slot));
                    current.store8(actor + 3092u, current.load8(actor + 3092u) | (direction < 0 ? 4u : 2u));
                    std::cout << "[secondary-items] selected " << slot << std::endl;
                }
                kernel().finish(returned, 0u);
            });
        return true;
    }
    // Grid taps select an actual pouch position; they never consume an item.
    if (*request >= 100 && *request < 132) {
        const unsigned slot = static_cast<unsigned>(*request - 100);
        if (slot < snapshot->capacity && snapshot->items[slot].id && snapshot->items[slot].count &&
            (ram.load8(0x089D0FA0u + 20u * snapshot->items[slot].id + 7u) & 8u)) {
            ram.store16(actor + 3052u, static_cast<std::uint16_t>(slot));
            std::cout << "[secondary-items] selected " << slot << std::endl;
        }
        return false;
    }
    if (*request != 0 || (ram.load32(actor + 636u) & 8u) || ram.load8(actor + 396u) != 0u ||
        ram.load8(actor + 397u) != 0u) return false;
    const auto &item = snapshot->items[snapshot->selected];
    if (!item.id || !item.count) return false;
    const auto uid = kernel().allocate_block("secondary-item-command", 0u, 64u, 0u);
    const auto *block = kernel().find_block(uid);
    if (!block) return false;
    const auto scratch = block->address;
    for (unsigned i = 0; i < 64u; ++i) ram.store8(scratch + i, 0u);
    ram.store16(scratch + 8u, 0xFFFFu);
    ctx.gpr[8] = scratch + 16u;
    ctx.gpr[9] = snapshot->selected;
    ctx.gpr[10] = scratch + 8u;
    // Original dispatcher: resolve the selected pouch slot, then submit the
    // returned action through the game's own transition routine. No pad bits.
    kernel().call_guest(ctx, 0x0A108BB0u, {actor, scratch, scratch + 4u, scratch + 6u},
        [actor, scratch, uid, id = item.id](auto &returned, std::uint32_t ok) {
            GuestRam current(kernel().runtime().memory());
            const auto now = read(current);
            const auto group = current.load8(scratch);
            const auto action = current.load16(scratch + 4u);
            const auto flags = current.load16(scratch + 6u);
            const auto auxiliary = current.load16(scratch + 8u);
            kernel().free_block(uid);
            if (!ok || !now || now->actor != actor || group == 255u || action == 255u) {
                std::cout << "[secondary-items] use refused" << std::endl;
                kernel().finish(returned, 0u);
                return;
            }
            current.store16(actor + 3124u, id);
            if (flags & 0x20u) current.store16(actor + 394u, auxiliary);
            std::cout << "[secondary-items] use " << id << " action " << unsigned(group) << ':' << action << std::endl;
            kernel().call_guest(returned, 0x0A0F72C8u, {actor, group, action, flags},
                [](auto &done, std::uint32_t) { kernel().finish(done, 0u); });
        });
    return true;
}
#endif
}
