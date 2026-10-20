// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/screen.cpp (GfxScreen::drawLine),
//   engines/sci/graphics/picture.cpp (GfxPicture::vectorPattern)
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
//
// Which pixels path 1 sets for a line and for a pattern, so that the vector
// path can relate its geometry to the 1x maps.
#pragma once

#include "sci/pic/picture_maps.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace sci::pic {

struct ScreenPoint {
    int x = 0;
    int y = 0;

    friend bool operator==(const ScreenPoint&, const ScreenPoint&) = default;
};

// ScummVM clips line end points to the screen before drawing (LSL3 room 620
// draws to x 320).
[[nodiscard]] constexpr ScreenPoint clip_to_screen(ScreenPoint p) noexcept {
    return {.x = std::clamp(p.x, 0, screen_width - 1), .y = std::clamp(p.y, 0, screen_height - 1)};
}

// GfxScreen::drawLine: calls visit(x, y) for every pixel of the line between
// two screen points, after clipping them, in ScummVM's order. Pixels may be
// visited twice.
template <typename Visit>
constexpr void for_each_line_pixel(ScreenPoint from, ScreenPoint to, Visit&& visit) {
    const ScreenPoint start = clip_to_screen(from);
    const ScreenPoint end = clip_to_screen(to);
    int left = start.x;
    int top = start.y;
    int right = end.x;
    int bottom = end.y;

    if (top == bottom) {
        if (right < left) {
            std::swap(right, left);
        }
        for (int x = left; x <= right; ++x) {
            visit(x, top);
        }
        return;
    }
    if (left == right) {
        if (top > bottom) {
            std::swap(top, bottom);
        }
        for (int y = top; y <= bottom; ++y) {
            visit(left, y);
        }
        return;
    }

    // Do not replace with another Bresenham: flood fills depend on exactly
    // which pixels this variant sets.
    const int step_y = bottom - top < 0 ? -1 : 1;
    const int step_x = right - left < 0 ? -1 : 1;
    const int dy = std::abs(bottom - top) * 2;
    const int dx = std::abs(right - left) * 2;
    visit(left, top);
    visit(right, bottom);
    if (dx > dy) {
        int fraction = dy - (dx / 2); // dx is not negative: dx / 2 == dx >> 1.
        while (left != right) {
            if (fraction >= 0) {
                top += step_y;
                fraction -= dx;
            }
            left += step_x;
            fraction += dy;
            visit(left, top);
        }
    } else {
        int fraction = dx - (dy / 2);
        while (top != bottom) {
            if (fraction >= 0) {
                left += step_x;
                fraction -= dy;
            }
            top += step_y;
            fraction += dx;
            visit(left, top);
        }
    }
}

// The pattern code: pen size (bits 0-2), rectangle instead of circle (bit
// 4), textured (bit 5).
[[nodiscard]] constexpr int pattern_size(std::uint8_t code) noexcept {
    return code & 0x07;
}

[[nodiscard]] constexpr bool pattern_is_rectangle(std::uint8_t code) noexcept {
    return (code & 0x10U) != 0;
}

[[nodiscard]] constexpr bool pattern_is_textured(std::uint8_t code) noexcept {
    return (code & 0x20U) != 0;
}

// A box of screen pixels: [left, right) x [top, bottom).
struct PixelBox {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// Where a pattern centred on the picture point (x, y) is drawn, in screen
// coordinates: centred on the point and one pixel wider than high, moved
// inside the left and top edges of the picture, offset into the picture
// port, then moved inside the screen. On the right, the box may keep its
// last column beyond the edge (one pixel), as in ScummVM; path 1 draws that
// column at the start of the next row.
[[nodiscard]] constexpr PixelBox pattern_box(int x, int y, int size) noexcept {
    const int width = (2 * size) + 2;
    const int height = (2 * size) + 1;
    PixelBox box{.left = x - size, .top = y - size, .right = x + size + 2, .bottom = y + size + 1};
    if (box.left < 0) {
        box = PixelBox{.left = 0, .top = box.top, .right = width, .bottom = box.bottom};
    }
    if (box.top < 0) {
        box = PixelBox{.left = box.left, .top = 0, .right = box.right, .bottom = height};
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

} // namespace sci::pic
