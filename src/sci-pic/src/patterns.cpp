// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/picture.cpp (GfxPicture::vectorPattern,
//       vectorPatternBox, vectorPatternTexturedBox, vectorPatternBoxPixel,
//       vectorPatternCircle, vectorPatternTexturedCircle)
// and translated into this project's style, types and error handling.
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#include "patterns.hpp"

#include "pattern_tables.hpp"
#include "sci/core/assert.hpp"

#include <cstddef>

namespace sci::pic::detail {

namespace {

constexpr std::uint8_t pattern_rectangle = 0x10;
constexpr std::uint8_t pattern_textured = 0x20;
constexpr std::uint8_t pattern_pen_size = 0x07;

// A pattern box in screen coordinates: [left, right) x [top, bottom).
struct Box {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// Where the pattern is drawn: centred on the point and one pixel wider than
// high, moved inside the left and top edges of the picture, offset into the
// picture port, then moved inside the screen. On the right, the box may keep
// its last column beyond the edge (one pixel), as in ScummVM; that column is
// handled by draw_box_pixel.
Box pattern_box(int x, int y, int size) {
    const int width = (2 * size) + 2;
    const int height = (2 * size) + 1;
    Box box{.left = x - size, .top = y - size, .right = x + size + 2, .bottom = y + size + 1};
    if (box.left < 0) {
        box = Box{.left = 0, .top = box.top, .right = width, .bottom = box.bottom};
    }
    if (box.top < 0) {
        box = Box{.left = box.left, .top = 0, .right = box.right, .bottom = height};
    }
    box.top += picture_top;
    box.bottom += picture_top;
    if (box.right > screen_width + 1) {
        box.left = screen_width + 1 - width;
        box.right = screen_width + 1;
    }
    if (box.bottom > screen_height) {
        box.top = screen_height - height;
        box.bottom = screen_height;
    }
    return box;
}

// The dither code with its two colours swapped.
std::uint8_t swap_dither(std::uint8_t color) noexcept {
    if ((color & 0xF0U) == 0) {
        return color;
    }
    auto value = static_cast<std::uint8_t>(color ^ (color << 4U));    // Decode.
    value = static_cast<std::uint8_t>((value << 4U) | (value >> 4U)); // Swap.
    return static_cast<std::uint8_t>(value ^ (value << 4U));          // Re-encode.
}

struct Stroke {
    std::uint8_t mask = 0;
    std::uint8_t color = 0;
    std::uint8_t priority = 0;
    std::uint8_t control = 0;
};

// vectorPatternBoxPixel. A pixel in the column beyond the right edge wraps
// to the start of the next row with the dither colours swapped, as in
// Sierra's interpreter.
void draw_box_pixel(Canvas& canvas, int x, int y, const Stroke& stroke) {
    if (x < 0 || y < 0 || y >= screen_height) {
        return;
    }
    if (x < screen_width) {
        canvas.put(x, y, stroke.mask, stroke.color, stroke.priority, stroke.control);
    } else if (y < screen_height - 1) {
        canvas.put(0, y + 1, stroke.mask, swap_dither(stroke.color), stroke.priority, stroke.control);
    }
}

// Reads Sierra's texture bits from a texture's start offset onwards.
class TextureBits {
public:
    explicit TextureBits(std::uint8_t texture) : position_(texture_offsets[texture & 0x7FU]) {}

    bool next() {
        SCI_ASSERT(position_ < texture_bits.size());
        return texture_bits[position_++] != 0;
    }

private:
    std::size_t position_;
};

void draw_box(Canvas& canvas, const Box& box, const Stroke& stroke, bool textured, std::uint8_t texture) {
    TextureBits bits(texture);
    for (int y = box.top; y < box.bottom; ++y) {
        for (int x = box.left; x < box.right; ++x) {
            if (!textured || bits.next()) {
                draw_box_pixel(canvas, x, y, stroke);
            }
        }
    }
}

// Circles: the size's bitmap gives one bit per box pixel. For textured
// circles the texture advances only on the circle's pixels.
void draw_circle(Canvas& canvas, const Box& box, int size, const Stroke& stroke, bool textured, std::uint8_t texture) {
    const auto& bitmap = circle_bitmaps[static_cast<std::size_t>(size)];
    std::size_t bit = 0;
    TextureBits bits(texture);
    for (int y = box.top; y < box.bottom; ++y) {
        for (int x = box.left; x < box.right; ++x, ++bit) {
            SCI_ASSERT(bit / 8 < bitmap.size());
            if (((bitmap[bit / 8] >> (bit % 8)) & 1U) == 0) {
                continue;
            }
            if (textured && !bits.next()) {
                continue;
            }
            if (Canvas::on_screen(x, y)) {
                canvas.put(x, y, stroke.mask, stroke.color, stroke.priority, stroke.control);
            }
        }
    }
}

} // namespace

void draw_pattern(Canvas& canvas, int x, int y, std::uint8_t color, std::uint8_t priority, std::uint8_t control,
                  std::uint8_t code, std::uint8_t texture) {
    const int size = code & pattern_pen_size;
    const Box box = pattern_box(x, y, size);
    const Stroke stroke{
        .mask = drawing_mask(color, priority, control),
        .color = color,
        .priority = priority,
        .control = control,
    };
    const bool textured = (code & pattern_textured) != 0;
    if ((code & pattern_rectangle) != 0) {
        draw_box(canvas, box, stroke, textured, texture);
    } else {
        draw_circle(canvas, box, size, stroke, textured, texture);
    }
}

} // namespace sci::pic::detail
