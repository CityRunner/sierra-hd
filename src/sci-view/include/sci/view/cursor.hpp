// SPDX-License-Identifier: GPL-3.0-or-later
//
// SCI0 cursor resources: a 16 x 16 shape of black, white and transparent
// pixels and a hotspot, as ScummVM's GfxCursor::kernelSetShape decodes them.
// The interpreter reports the shape (milestone 6b's layout file) and the
// program shows it scaled N x N (spec section 6.9).

#pragma once

#include "sci/core/error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sci::view {

enum class CursorPixel : std::uint8_t {
    Black,
    White,
    Transparent,
};

struct Cursor {
    static constexpr int size = 16;

    std::array<CursorPixel, std::size_t{size} * size> pixels{}; // Row by row.
    int hotspot_x = 0;
    int hotspot_y = 0;

    [[nodiscard]] CursorPixel at(int x, int y) const {
        return pixels[(static_cast<std::size_t>(y) * size) + static_cast<std::size_t>(x)];
    }
};

// Decodes a cursor resource: 68 bytes, the hotspot flag in byte 3, then 16
// words of mask A and 16 of mask B. Any other size is InvalidData.
[[nodiscard]] core::Result<Cursor> decode_cursor(std::span<const std::uint8_t> resource);

} // namespace sci::view
