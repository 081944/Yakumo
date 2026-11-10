#include "game/secondary_map.hpp"
#include "game/control_context.hpp"
#include "platform/android_secondary_screen.hpp"
#include "psprecomp/runtime.hpp"
#include <cstdlib>
#include <iostream>

#if defined(MHP3RD_HUD_FUNCTIONS_INC)
namespace psprecomp {
#define MHP3RD_HUD_FUNCTION(address, unit) void unit(Runtime &, AllegrexContext &);
#include MHP3RD_HUD_FUNCTIONS_INC
#undef MHP3RD_HUD_FUNCTION
}
#endif

namespace mhp3rd::game::secondary_map {
namespace {
constexpr std::uint32_t kInventoryQuery = 0x0886FB5Cu;
psprecomp::Runtime::RecompiledFunction original{};

void query(psprecomp::Runtime &runtime, psprecomp::AllegrexContext &ctx) {
    static const bool enabled = std::getenv("MHP3RD_NO_SECONDARY_MAP") == nullptr &&
        std::getenv("MHP3RD_SECONDARY_MAP_REQUIRE_ITEM") == nullptr;
    const bool supported = runtime.memory().load32(0x0A16650Cu) == 0x27BDFFF0u &&
        runtime.memory().load32(0x0A166548u) == 0x2405029Eu &&
        runtime.memory().load32(0x0A166574u) == 0x0E21BED7u;
    if (reveal(android::second_screen_wanted(), enabled, control_context::current().scene == control_context::Scene::Quest,
            supported, ctx.gpr[31], ctx.gpr[5])) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            std::cout << "[secondary-map] revealing map for lower display" << std::endl;
        }
        ctx.gpr[2] = 1u;
        ctx.pc = ctx.gpr[31];
        return;
    }
    original(runtime, ctx);
}
}

void frame(psprecomp::Runtime &runtime) {
    if (original || !android::second_screen_wanted()) return;
    if (runtime.memory().load32(kInventoryQuery) != 0x27BDFFE0u ||
        runtime.memory().load32(kInventoryQuery + 4u) != 0xAFB3000Cu) return;
#if defined(MHP3RD_HUD_FUNCTIONS_INC)
#define MHP3RD_HUD_FUNCTION(address, unit) if constexpr (address == kInventoryQuery) original = &psprecomp::unit;
#include MHP3RD_HUD_FUNCTIONS_INC
#undef MHP3RD_HUD_FUNCTION
#endif
    if (!original) return;
    runtime.register_function(kInventoryQuery, &query, "secondary_map_inventory_query");
    std::cout << "[secondary-map] map possession independent of inventory" << std::endl;
}
}
