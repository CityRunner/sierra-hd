// SPDX-License-Identifier: GPL-3.0-or-later
//
// SCI0 EGA views: loops of cels, each a small bitmap of EGA colours with a
// transparent colour and a displacement from the object's position. Decoding
// follows ScummVM's GfxView, and every cel bitmap is the one GfxView::
// getBitmap returns (mirrored loops already mirrored).

#pragma once

#include "sci/core/error.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::view {

struct Cel {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::int16_t displace_x = 0; // Negated for mirrored loops, as in ScummVM.
    std::uint8_t displace_y = 0;
    std::uint8_t clear_key = 0;       // The transparent colour.
    std::vector<std::uint8_t> pixels; // width * height EGA colours, row by row.
};

struct Loop {
    bool mirrored = false;
    std::vector<Cel> cels;
};

struct View {
    std::vector<Loop> loops;
};

// Decodes a view resource. SCI1 and SCI1.1 views are Unsupported; offsets or
// pixel data outside the resource are InvalidData.
[[nodiscard]] core::Result<View> decode_view(std::span<const std::uint8_t> resource);

} // namespace sci::view
