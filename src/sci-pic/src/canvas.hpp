// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pixel access for the rasteriser, as ScummVM's GfxScreen provides it while
// a picture is drawn. During drawing the visual map holds ScummVM's encoded
// colour: an EGA pair e (high nibble h, low nibble l) is stored as
// e ^ (e << 4), so solid colours (h == l) are plain 0-15 and dithered ones
// have a non-zero high nibble. dither() turns the codes into colours at the
// end, as GfxScreen::dither() does.

#pragma once

#include "sci/pic/picture_maps.hpp"

#include <cstdint>

namespace sci::pic::detail {

enum MapMask : std::uint8_t {
    mask_visual = 1,
    mask_priority = 2,
    mask_control = 4,
};

// The value 255 disables drawing into a map (GfxScreen::getDrawingMask).
inline constexpr std::uint8_t disabled = 255;

[[nodiscard]] constexpr std::uint8_t drawing_mask(std::uint8_t color, std::uint8_t priority,
                                                  std::uint8_t control) noexcept {
    return static_cast<std::uint8_t>((color != disabled ? mask_visual : 0) |
                                     (priority != disabled ? mask_priority : 0) |
                                     (control != disabled ? mask_control : 0));
}

// The pair an encoded colour stands for.
[[nodiscard]] constexpr ColorPair decode_pair(std::uint8_t code) noexcept {
    if ((code & 0xF0U) == 0) {
        return static_cast<ColorPair>(code * 0x11U);
    }
    const unsigned value = code;
    return static_cast<ColorPair>((value ^ (value << 4U)) & 0xFFU);
}

// The colour an encoded value shows at (x, y): the visible EGA colour that
// ScummVM compares in flood fills (vectorIsFillMatch for EGA).
[[nodiscard]] constexpr std::uint8_t visible_color(std::uint8_t code, int x, int y) noexcept {
    return (((x ^ y) & 1) != 0) ? static_cast<std::uint8_t>((code ^ (code >> 4U)) & 0x0FU)
                                : static_cast<std::uint8_t>(code & 0x0FU);
}

class Canvas {
public:
    explicit Canvas(PictureMaps& maps) noexcept : maps_(maps) {}

    [[nodiscard]] static constexpr bool on_screen(int x, int y) noexcept {
        return x >= 0 && x < screen_width && y >= 0 && y < screen_height;
    }

    // GfxScreen::putPixel / vectorPutPixel at 320x200, plus the pair map.
    void put(int x, int y, std::uint8_t mask, std::uint8_t color, std::uint8_t priority,
             std::uint8_t control) noexcept {
        const std::size_t i = PictureMaps::index(x, y);
        if ((mask & mask_visual) != 0) {
            maps_.visual[i] = color;
            maps_.pairs[i] = decode_pair(color);
        }
        if ((mask & mask_priority) != 0) {
            maps_.priority[i] = priority;
        }
        if ((mask & mask_control) != 0) {
            maps_.control[i] = control;
        }
    }

    [[nodiscard]] std::uint8_t code(int x, int y) const noexcept { return maps_.visual[PictureMaps::index(x, y)]; }

    [[nodiscard]] std::uint8_t priority(int x, int y) const noexcept {
        return maps_.priority[PictureMaps::index(x, y)];
    }

    [[nodiscard]] std::uint8_t control(int x, int y) const noexcept { return maps_.control[PictureMaps::index(x, y)]; }

    // GfxScreen::drawLine: Sierra's Bresenham variant, with both end points
    // clipped to the screen first.
    void draw_line(int x0, int y0, int x1, int y1, std::uint8_t color, std::uint8_t priority, std::uint8_t control);

    // GfxScreen::dither, without undithering: replaces every encoded colour
    // by the colour it shows at its position.
    void dither() noexcept;

private:
    PictureMaps& maps_;
};

// GfxPicture::vectorFloodFill, for EGA pictures in the picture port. (x, y)
// are picture coordinates, already mirrored; the fill starts at screen row
// y + picture_top. Returns false if that point is off the screen, where
// ScummVM would read outside its buffers.
[[nodiscard]] bool flood_fill(Canvas& canvas, int x, int y, std::uint8_t color, std::uint8_t priority,
                              std::uint8_t control);

} // namespace sci::pic::detail
