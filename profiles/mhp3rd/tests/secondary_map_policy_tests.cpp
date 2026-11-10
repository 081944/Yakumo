#include "gpu/secondary_map_policy.hpp"
#include "game/secondary_map.hpp"
#include <cstdlib>
#include <iostream>
using namespace mhp3rd::gpu;
void require(bool value) {
    if (!value) std::abort();
}
int main() {
    const auto reveal = mhp3rd::game::secondary_map::reveal;
    for (bool lower : {false, true})
        for (bool enabled : {false, true})
            for (bool quest : {false, true})
                for (bool supported : {false, true}) {
                    require(reveal(lower, enabled, quest, supported, 0x0A16657Cu, 670u) ==
                        (lower && enabled && quest && supported));
                    require(!reveal(lower, enabled, quest, supported, 0x0A16657Cu, 669u));
                    require(!reveal(lower, enabled, quest, supported, 0x0A16657Cu, 671u));
                    require(!reveal(lower, enabled, quest, supported, 0x0A166580u, 670u));
                }
    using secondary_map::Update;
    // Independent visible-content oracle: a completed map survives arbitrary
    // intermediate presents and empty original frames, until an actual exit.
    bool visible = false;
    const auto apply = [&](bool quest, bool original, bool draws) {
        const auto action = secondary_map::update(quest, original, draws);
        if (action == Update::Clear) visible = false;
        if (action == Update::Render) visible = true;
    };
    apply(false, true, false);
    require(!visible);
    apply(true, true, true);
    require(visible);
    for (unsigned i = 0; i < 120; ++i) {
        apply(true, false, false);
        require(visible);
        apply(true, false, true);
        require(visible);
        apply(true, true, false);
        require(visible);
        apply(true, true, true);
        require(visible);
    }
    require(secondary_map::update(true, false, true) == Update::Hold);
    require(secondary_map::update(true, true, false) == Update::Hold);
    apply(false, false, true);
    require(!visible);
    apply(true, true, false);
    require(!visible);
    apply(true, true, true);
    require(visible);
    apply(false, true, false);
    require(!visible);
    for (bool available : {false, true})
        for (bool enabled : {false, true})
            for (bool mirror : {false, true}) {
                unsigned hidden = 0;
                for (auto part :
                    {hud::Part::None, hud::Part::Other, hud::Part::Status, hud::Part::Map, hud::Part::Items})
                    hidden += secondary_map::hide_main(available, enabled, mirror, part);
                require(hidden == unsigned(available && enabled && !mirror));
            }
    std::cout << "secondary map continuity and routing contracts passed\n";
}
