// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "canvas.hpp"

#include <cstdint>

namespace sci::pic::detail {

// GfxPicture::vectorPattern: a brush stroke centred on (x, y) in picture
// coordinates (already mirrored). The pattern code selects the pen size
// (bits 0-2), a rectangle (bit 4) instead of a circle, and the texture (bit
// 5), which then comes from Sierra's 255-bit noise table at the texture
// number's offset.
void draw_pattern(Canvas& canvas, int x, int y, std::uint8_t color, std::uint8_t priority, std::uint8_t control,
                  std::uint8_t code, std::uint8_t texture);

} // namespace sci::pic::detail
