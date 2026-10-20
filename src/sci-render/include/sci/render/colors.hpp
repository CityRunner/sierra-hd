// SPDX-License-Identifier: GPL-3.0-or-later
//
// Colours of the HD mode (spec section 6.5). An EGA pair is shown as the
// plain average of its two colours' sRGB values, without linear-light
// conversion. Internal values have 16 bits per channel (an 8-bit value v
// is v * 257); the table holds the sum of the pair's two colours, so that
// averages can be taken later with a single rounding.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sci::render {

struct Rgb8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend constexpr bool operator==(const Rgb8&, const Rgb8&) = default;
};

// The 16 EGA colours, as ScummVM sets them (GfxPalette::setEGA).
inline constexpr std::array<Rgb8, 16> ega_palette{
    Rgb8{.r = 0x00, .g = 0x00, .b = 0x00}, Rgb8{.r = 0x00, .g = 0x00, .b = 0xAA}, Rgb8{.r = 0x00, .g = 0xAA, .b = 0x00},
    Rgb8{.r = 0x00, .g = 0xAA, .b = 0xAA}, Rgb8{.r = 0xAA, .g = 0x00, .b = 0x00}, Rgb8{.r = 0xAA, .g = 0x00, .b = 0xAA},
    Rgb8{.r = 0xAA, .g = 0x55, .b = 0x00}, Rgb8{.r = 0xAA, .g = 0xAA, .b = 0xAA}, Rgb8{.r = 0x55, .g = 0x55, .b = 0x55},
    Rgb8{.r = 0x55, .g = 0x55, .b = 0xFF}, Rgb8{.r = 0x55, .g = 0xFF, .b = 0x55}, Rgb8{.r = 0x55, .g = 0xFF, .b = 0xFF},
    Rgb8{.r = 0xFF, .g = 0x55, .b = 0x55}, Rgb8{.r = 0xFF, .g = 0x55, .b = 0xFF}, Rgb8{.r = 0xFF, .g = 0xFF, .b = 0x55},
    Rgb8{.r = 0xFF, .g = 0xFF, .b = 0xFF},
};

// The sum of a pair's two colours at 16 bits per channel (up to 2 * 65535).
struct PairSum {
    std::uint32_t r = 0;
    std::uint32_t g = 0;
    std::uint32_t b = 0;
};

// Indexed by the pair (high nibble: colour at odd positions, low nibble:
// at even positions), computed at build time.
inline constexpr std::array<PairSum, 256> pair_sums = [] {
    std::array<PairSum, 256> table{};
    for (std::size_t pair = 0; pair < table.size(); ++pair) {
        const Rgb8 odd = ega_palette[pair >> 4U];
        const Rgb8 even = ega_palette[pair & 0x0FU];
        table[pair] = PairSum{
            .r = (std::uint32_t{odd.r} + even.r) * 257U,
            .g = (std::uint32_t{odd.g} + even.g) * 257U,
            .b = (std::uint32_t{odd.b} + even.b) * 257U,
        };
    }
    return table;
}();

// A 16-bit channel value as 8 bits, rounded to the nearest value (halves
// upwards): round(v * 255 / 65535), computed with a shift.
[[nodiscard]] constexpr std::uint8_t to_8bit(std::uint32_t value16) noexcept {
    return static_cast<std::uint8_t>(((value16 * 255U) + 32768U) >> 16U);
}

// The colour of a pair: the average of its two colours, halves upwards.
[[nodiscard]] constexpr Rgb8 pair_color(std::uint8_t pair) noexcept {
    const PairSum& sum = pair_sums[pair];
    return {.r = to_8bit((sum.r + 1) >> 1U), .g = to_8bit((sum.g + 1) >> 1U), .b = to_8bit((sum.b + 1) >> 1U)};
}

} // namespace sci::render
