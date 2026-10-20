// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 1x maps a picture is drawn into (path 1, spec section 6.2): visual,
// priority and control as the game and ScummVM see them, plus the pair map
// that the vector path reads. Each map is 320x200 bytes, row by row; the
// picture itself occupies rows 10 to 199 (the picture port), rows 0 to 9
// belong to the menu bar.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sci::pic {

inline constexpr int screen_width = 320;
inline constexpr int screen_height = 200;
inline constexpr int picture_top = 10; // Top of the picture port on screen.
inline constexpr std::size_t map_size = static_cast<std::size_t>(screen_width) * screen_height;

// An EGA colour pair: the colour shown where (x ^ y) is even in the low
// nibble, the colour where it is odd in the high nibble. A solid colour c is
// c * 0x11. This is the decoded form of the dither code ScummVM keeps in the
// visual map while drawing.
using ColorPair = std::uint8_t;

// Whether (x ^ y) is odd: the dither phase of a position.
[[nodiscard]] constexpr bool odd_position(int x, int y) noexcept {
    return ((static_cast<unsigned>(x) ^ static_cast<unsigned>(y)) & 1U) != 0;
}

[[nodiscard]] constexpr std::uint8_t pair_color_at(ColorPair pair, int x, int y) noexcept {
    const unsigned value = pair;
    return static_cast<std::uint8_t>(odd_position(x, y) ? (value >> 4U) : (value & 0x0FU));
}

struct PictureMaps {
    std::vector<std::uint8_t> visual = std::vector<std::uint8_t>(map_size, 0);   // EGA colour 0-15
    std::vector<std::uint8_t> priority = std::vector<std::uint8_t>(map_size, 0); // 0-15
    std::vector<std::uint8_t> control = std::vector<std::uint8_t>(map_size, 0);  // 0-15
    std::vector<ColorPair> pairs = std::vector<ColorPair>(map_size, 0);          // Pair behind each visual pixel

    [[nodiscard]] static constexpr std::size_t index(int x, int y) noexcept {
        return (static_cast<std::size_t>(y) * screen_width) + static_cast<std::size_t>(x);
    }
};

} // namespace sci::pic
