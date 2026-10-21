// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   common/rect.h (Common::Rect, the operations the SCI0 graphics use)
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

#pragma once

#include <algorithm>
#include <cstdint>

namespace sci::vm {

struct GPoint {
    std::int16_t x = 0;
    std::int16_t y = 0;
    friend constexpr bool operator==(GPoint, GPoint) noexcept = default;
};

// A rectangle with exclusive right and bottom edges, as Common::Rect.
struct Rect {
    std::int16_t top = 0;
    std::int16_t left = 0;
    std::int16_t bottom = 0;
    std::int16_t right = 0;

    [[nodiscard]] static constexpr Rect from(int left, int top, int right, int bottom) noexcept {
        return Rect{.top = static_cast<std::int16_t>(top),
                    .left = static_cast<std::int16_t>(left),
                    .bottom = static_cast<std::int16_t>(bottom),
                    .right = static_cast<std::int16_t>(right)};
    }

    [[nodiscard]] constexpr int width() const noexcept { return right - left; }

    [[nodiscard]] constexpr int height() const noexcept { return bottom - top; }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return left >= right || top >= bottom; }

    [[nodiscard]] constexpr bool is_valid() const noexcept { return left <= right && top <= bottom; }

    [[nodiscard]] constexpr bool contains(int x, int y) const noexcept {
        return left <= x && x < right && top <= y && y < bottom;
    }

    constexpr void clip(const Rect& r) noexcept {
        if (top < r.top) {
            top = r.top;
        } else if (top > r.bottom) {
            top = r.bottom;
        }
        if (left < r.left) {
            left = r.left;
        } else if (left > r.right) {
            left = r.right;
        }
        if (bottom > r.bottom) {
            bottom = r.bottom;
        } else if (bottom < r.top) {
            bottom = r.top;
        }
        if (right > r.right) {
            right = r.right;
        } else if (right < r.left) {
            right = r.left;
        }
    }

    constexpr void extend(const Rect& r) noexcept {
        left = std::min(left, r.left);
        right = std::max(right, r.right);
        top = std::min(top, r.top);
        bottom = std::max(bottom, r.bottom);
    }

    constexpr void grow(int offset) noexcept {
        top = static_cast<std::int16_t>(top - offset);
        left = static_cast<std::int16_t>(left - offset);
        bottom = static_cast<std::int16_t>(bottom + offset);
        right = static_cast<std::int16_t>(right + offset);
    }

    constexpr void move_to(int x, int y) noexcept {
        bottom = static_cast<std::int16_t>(bottom + (y - top));
        right = static_cast<std::int16_t>(right + (x - left));
        top = static_cast<std::int16_t>(y);
        left = static_cast<std::int16_t>(x);
    }

    constexpr void translate(int dx, int dy) noexcept {
        left = static_cast<std::int16_t>(left + dx);
        right = static_cast<std::int16_t>(right + dx);
        top = static_cast<std::int16_t>(top + dy);
        bottom = static_cast<std::int16_t>(bottom + dy);
    }

    friend constexpr bool operator==(const Rect&, const Rect&) noexcept = default;
};

} // namespace sci::vm
