// The bridge between the renderer and SecondaryScreen.java: one map image in,
// item shortcut taps out. Nothing here knows about Vulkan or the game; the
// renderer hands over the pixels it has already drawn and asks once a frame
// whether a shortcut was tapped.

#include "platform/android_secondary_screen.hpp"

#include <jni.h>

#include <cstddef>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <vector>
#include <array>
#include <string>
#include "game/game_data.hpp"
#include "game/item_icon_source.hpp"
#include "gpu/texture_decode.hpp"
#include <map>

namespace mhp3rd::android {
namespace {

// A map region is at most a few hundred pixels square; four megabytes leaves
// room for a large internal resolution without ever growing the buffer.
constexpr std::size_t kBufferBytes = 4u * 1024u * 1024u;

std::mutex g_mutex;
std::vector<std::uint8_t> g_pixels; // RGBA, tightly packed
int g_width{};
int g_height{};
bool g_dirty{};
bool g_wanted{};
bool g_force_map{};
std::deque<int> g_items;
std::array<std::string, 33> g_item_state;
std::map<unsigned, std::vector<jint>> g_original_icons;

void *g_buffer{};
std::size_t g_buffer_bytes{};

} // namespace

void publish_second_screen_icons(
    psprecomp::GuestMemory &memory, const std::optional<game::secondary_items::Snapshot> &items) {
    if (!items) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_original_icons.clear();
        return;
    }
    game::GuestRam ram(memory);
    for (unsigned slot = 0; slot < items->capacity; ++slot) {
        const auto &item = items->items[slot];
        if (!item.id || !item.count) continue;
        const unsigned key = item.icon * 256u + item.color;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_original_icons.contains(key)) continue;
        }
        const auto source = game::item_icons::read(ram, item.icon, item.color);
        if (!source) continue;
        gpu::TextureState texture{};
        texture.enabled = true;
        texture.address = source->texels;
        texture.width = static_cast<std::uint16_t>(source->width);
        texture.height = static_cast<std::uint16_t>(source->height);
        texture.buffer_width = source->width;
        texture.format = static_cast<gpu::TextureFormat>(source->format);
        texture.swizzled = true;
        texture.clut_address = source->palette;
        texture.clut_format = source->palette_format;
        texture.clut_mask = 255u;
        texture.clut_load_bytes = source->palette_format == 3u ? 64u : 32u;
        texture.clut_max_bytes = texture.clut_load_bytes;
        std::vector<std::uint32_t> pixels;
        if (!gpu::decode_texture(memory, texture, pixels)) continue;
        std::vector<jint> icon(2u + source->crop_width * source->crop_height);
        icon[0] = static_cast<jint>(source->crop_width);
        icon[1] = static_cast<jint>(source->crop_height);
        for (unsigned y = 0; y < source->crop_height; ++y) {
            for (unsigned x = 0; x < source->crop_width; ++x) {
                const auto rgba = pixels[(source->y + y) * source->width + source->x + x];
                icon[2u + y * source->crop_width + x] =
                    static_cast<jint>((rgba & 0xFF00FF00u) | ((rgba & 255u) << 16u) | ((rgba >> 16u) & 255u));
            }
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_original_icons.emplace(key, std::move(icon));
    }
}

void publish_second_screen_items(const game::Ram &ram, const std::optional<game::secondary_items::Snapshot> &items) {
    std::array<std::string, 33> state;
    state[0] = items ? std::to_string(items->selected) : "-1";
    if (items) {
        for (unsigned i = 0; i < items->capacity; ++i) {
            const auto &item = items->items[i];
            if (!item.id || !item.count) continue;
            const auto name = game::text_entry(ram, game::kTextBlock, 3, item.id);
            state[i + 1] = std::to_string(item.count) + "\t" + std::to_string(item.icon) + "\t" +
                std::to_string(item.color) + "\t" + name;
        }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_item_state = std::move(state);
}

void set_second_screen_map(const std::uint8_t *pixels, int width, int height, bool bgra) {
    if (pixels == nullptr || width <= 0 || height <= 0) return;
    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (bytes > kBufferBytes) return;
    g_pixels.resize(bytes);
    if (bgra) {
        const std::uint8_t *src = pixels;
        std::uint8_t *dst = g_pixels.data();
        for (std::size_t i = 0; i < bytes; i += 4u) {
            dst[i + 0] = src[i + 2];
            dst[i + 1] = src[i + 1];
            dst[i + 2] = src[i + 0];
            dst[i + 3] = 255u;
        }
    } else {
        std::memcpy(g_pixels.data(), pixels, bytes);
        for (std::size_t i = 3; i < bytes; i += 4u) g_pixels[i] = 255u;
    }
    g_width = width;
    g_height = height;
    g_dirty = true;
}

bool second_screen_map_size(int &width, int &height) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_width <= 0 || g_height <= 0) return false;
    width = g_width;
    height = g_height;
    return true;
}

void clear_second_screen_map() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_width = 0;
    g_height = 0;
    g_dirty = false;
    g_pixels.clear();
}

std::optional<int> take_second_screen_item_request() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_items.empty()) return std::nullopt;
    const int request = g_items.front();
    g_items.pop_front();
    // Handy proof, in logcat and yakumo.log, that a tap on the secondary
    // display really reached the renderer's pad sampling.
    std::cout << "[pad] lower-screen shortcut " << request << std::endl;
    return request;
}

bool second_screen_wanted() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_wanted;
}

void set_second_screen_map_forced(bool forced) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_force_map = forced;
}

bool second_screen_map_forced() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_force_map;
}

} // namespace mhp3rd::android

extern "C" {

JNIEXPORT jintArray JNICALL Java_io_github_teamgdb_yakumo_SecondaryScreen_nativeItemIcon(
    JNIEnv *env, jclass, jint icon, jint color) {
    using namespace mhp3rd::android;
    if (icon < 0 || icon >= 71 || color < 0 || color >= 32) return nullptr;
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto found = g_original_icons.find(static_cast<unsigned>(icon * 256 + color));
    if (found == g_original_icons.end()) return nullptr;
    const auto &pixels = found->second;
    auto result = env->NewIntArray(static_cast<jsize>(pixels.size()));
    if (result) env->SetIntArrayRegion(result, 0, static_cast<jsize>(pixels.size()), pixels.data());
    return result;
}

JNIEXPORT jobjectArray JNICALL Java_io_github_teamgdb_yakumo_SecondaryScreen_nativeItemState(JNIEnv *env, jclass) {
    using namespace mhp3rd::android;
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto string_class = env->FindClass("java/lang/String");
    if (!string_class) return nullptr;
    auto result = env->NewObjectArray(33, string_class, nullptr);
    env->DeleteLocalRef(string_class);
    if (!result) return nullptr;
    for (int i = 0; i < 33; ++i) {
        auto entry = env->NewStringUTF(g_item_state[i].c_str());
        if (!entry) return nullptr;
        env->SetObjectArrayElement(result, i, entry);
        env->DeleteLocalRef(entry);
    }
    return result;
}

// SecondaryScreen.nativeMapBuffer(int, int)
JNIEXPORT jobject JNICALL Java_io_github_teamgdb_yakumo_SecondaryScreen_nativeMapBuffer(
    JNIEnv *env, jclass, jint, jint) {
    using namespace mhp3rd::android;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_buffer == nullptr) {
        g_buffer = std::calloc(1u, kBufferBytes);
        if (g_buffer == nullptr) return nullptr;
        g_buffer_bytes = kBufferBytes;
    }
    g_wanted = true;
    return env->NewDirectByteBuffer(g_buffer, static_cast<jlong>(g_buffer_bytes));
}

// SecondaryScreen.nativeMapSize(int[])
JNIEXPORT void JNICALL Java_io_github_teamgdb_yakumo_SecondaryScreen_nativeMapActive(JNIEnv *, jclass, jboolean active) {
    using namespace mhp3rd::android;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_wanted = active == JNI_TRUE && g_buffer != nullptr;
}

JNIEXPORT jboolean JNICALL Java_io_github_teamgdb_yakumo_SecondaryScreen_nativeMapSize(
    JNIEnv *env, jclass, jintArray out) {
    using namespace mhp3rd::android;
    if (out == nullptr || env->GetArrayLength(out) < 2) return JNI_FALSE;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_width <= 0 || g_height <= 0) return JNI_FALSE;
    const jint values[2] = {g_width, g_height};
    env->SetIntArrayRegion(out, 0, 2, values);
    return JNI_TRUE;
}

// SecondaryScreen.nativeCopyMap()
JNIEXPORT jboolean JNICALL Java_io_github_teamgdb_yakumo_SecondaryScreen_nativeCopyMap(JNIEnv *, jclass) {
    using namespace mhp3rd::android;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_dirty || g_buffer == nullptr || g_pixels.empty()) return JNI_FALSE;
    if (g_pixels.size() > g_buffer_bytes) return JNI_FALSE;
    std::memcpy(g_buffer, g_pixels.data(), g_pixels.size());
    g_dirty = false;
    return JNI_TRUE;
}

// SecondaryScreen.nativeItem(int)
JNIEXPORT void JNICALL Java_io_github_teamgdb_yakumo_SecondaryScreen_nativeItem(JNIEnv *, jclass, jint action) {
    using namespace mhp3rd::android;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (action != -1 && action != 0 && action != 1 && (action < 100 || action >= 132)) return;
    if (g_items.size() >= 8u) return;
    g_items.push_back(action);
}

// SecondaryScreen.nativeForceMap(boolean)
JNIEXPORT void JNICALL Java_io_github_teamgdb_yakumo_SecondaryScreen_nativeForceMap(JNIEnv *, jclass, jboolean forced) {
    mhp3rd::android::set_second_screen_map_forced(forced == JNI_TRUE);
}

} // extern "C"
