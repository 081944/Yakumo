#pragma once

#include <cstdint>
#include <optional>
#include "game/secondary_items.hpp"

// The lower screen of a two-screen Android handheld: the quest map and the
// item shortcuts (SecondaryScreen.java). The map comes from an independent
// pass containing only the game's Map draws. Item requests call its helpers on the
// guest thread at the end of the display import.
namespace mhp3rd::android {
void publish_second_screen_items(const game::Ram &ram, const std::optional<game::secondary_items::Snapshot> &items);
void publish_second_screen_icons(
    psprecomp::GuestMemory &memory, const std::optional<game::secondary_items::Snapshot> &items);

// One map image, handed over by the renderer. `pixels` is tightly packed
// 32-bit colour, `bgra` saying which order the channels are in. Called on the
// render thread; the Java side copies it out from the UI thread.
void set_second_screen_map(const std::uint8_t *pixels, int width, int height, bool bgra);

// The map image's size in pixels; false while the renderer has not drawn one.
bool second_screen_map_size(int &width, int &height);

// Forget the map image: outside a quest the game draws no minimap, and the
// lower screen shows its standby panel instead of a stale crop of a menu.
void clear_second_screen_map();

// An item shortcut the player tapped, as an index into the item bar: -1 for
// the previous item, 1 for the next, 0 for "use what is selected". Empty when
// nothing is waiting. Values 100..131 use the corresponding pouch slot.
// Read once per original game frame by the display import.
std::optional<int> take_second_screen_item_request();

// True when the Java side has asked for a map image, so the renderer knows it
// can skip the copy entirely while the lower screen is closed.
bool second_screen_wanted();

// Debug switch, set by SecondaryScreen.java from a "force_map" marker file in
// the app's external files directory. It bypasses the quest-HUD check so the
// whole readback path can be exercised on the title screen, where the copied
// rectangle is plainly visible. Off by default; a real quest never needs it.
void set_second_screen_map_forced(bool forced);
bool second_screen_map_forced();

} // namespace mhp3rd::android
