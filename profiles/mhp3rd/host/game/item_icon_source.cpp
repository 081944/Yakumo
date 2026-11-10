#include "game/item_icon_source.hpp"

namespace mhp3rd::game::item_icons {
namespace {
bool mapped(const Ram &ram, std::uint32_t at, std::size_t size) {
    return at >= 0x08800000u && at < 0x0C000000u && size <= 0x0C000000u - at && ram.contains(at, size);
}
}
std::optional<Source> read(const Ram &ram, unsigned icon, unsigned color) {
    if (icon >= 71u || color >= 32u || !ram.contains(0x09FF1DFCu, 4u)) return std::nullopt;
    const auto context = ram.load32(0x09FF1DFCu);
    if (!mapped(ram, context, 3256u)) return std::nullopt;
    const auto archive = ram.load32(context + 3200u);
    // The HMT texture archive bound by 0x08900658 through texture slot zero.
    if (!mapped(ram, archive, 48u) || ram.load32(archive) != 0x484D542Eu) return std::nullopt;
    const auto entry = archive + 16u;
    Source source;
    source.texels = entry + 32u;
    source.format = ram.load32(entry + 24u);
    source.width = ram.load16(entry + 28u);
    source.height = ram.load16(entry + 30u);
    if (source.format != 4u || !source.width || !source.height || source.width > 512u || source.height > 512u ||
        (source.width & (source.width - 1u)) || (source.height & (source.height - 1u)) ||
        !mapped(ram, source.texels, source.width * source.height / 2u)) return std::nullopt;
    auto palette_block = entry + 16u;
    const auto steps = ram.load32(entry + 8u);
    if (steps > 32u) return std::nullopt;
    for (unsigned i = 0; i < steps; ++i) {
        if (!mapped(ram, palette_block, 4u)) return std::nullopt;
        const auto distance = ram.load32(palette_block);
        if (distance < 16u || distance > 1024u * 1024u || distance > 0x0C000000u - palette_block) return std::nullopt;
        palette_block += distance;
    }
    if (!mapped(ram, palette_block, 16u) || !ram.contains(0x089FE63Cu + icon * 4u, 4u) ||
        !ram.contains(0x089FE758u + color * 4u, 2u)) return std::nullopt;
    source.palette_format = ram.load32(palette_block + 8u);
    if (source.palette_format > 3u) return std::nullopt;
    const auto palette_index = ram.load16(0x089FE758u + color * 4u);
    const auto entries = ram.load16(palette_block + 12u);
    if (palette_index >= 32u || (palette_index + 1u) * 16u > entries) return std::nullopt;
    const unsigned palette_bytes = source.palette_format == 3u ? 64u : 32u;
    source.palette = palette_block + 16u + palette_index * palette_bytes;
    if (!mapped(ram, source.palette, palette_bytes)) return std::nullopt;
    const auto sprite = ram.load32(0x089FE63Cu + icon * 4u);
    if (sprite > 255u) return std::nullopt;
    const auto rectangle = 0x089FEF24u + sprite * 8u;
    if (!ram.contains(rectangle, 8u)) return std::nullopt;
    source.x = ram.load16(rectangle);
    source.y = ram.load16(rectangle + 2u);
    source.crop_width = ram.load16(rectangle + 4u);
    source.crop_height = ram.load16(rectangle + 6u);
    if (!source.crop_width || !source.crop_height || source.crop_width > 64u || source.crop_height > 64u ||
        source.x + source.crop_width > source.width || source.y + source.crop_height > source.height) return std::nullopt;
    return source;
}
}
