// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/screen.cpp
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

#include "gfx_screen.hpp"

#include "sci/pic/picture_maps.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>

namespace sci::vm {

namespace {

constexpr std::size_t rect_bytes = 8; // sizeof(Common::Rect): four int16.

void write16(std::span<std::uint8_t> memory, std::size_t at, std::int16_t value) {
    const auto bits = static_cast<std::uint16_t>(value);
    memory[at] = static_cast<std::uint8_t>(bits & 0xFFU);
    memory[at + 1] = static_cast<std::uint8_t>(bits >> 8U);
}

std::int16_t read16(std::span<const std::uint8_t> memory, std::size_t at) {
    return static_cast<std::int16_t>(static_cast<unsigned>(memory[at]) | (static_cast<unsigned>(memory[at + 1]) << 8U));
}

} // namespace

Screen::Screen()
    : visual_(static_cast<std::size_t>(width) * height, 0), priority_(static_cast<std::size_t>(width) * height, 0),
      control_(static_cast<std::size_t>(width) * height, 0), cover_(static_cast<std::size_t>(width) * height, 1),
      shown_(static_cast<std::size_t>(width) * height, 0), shown_cover_(static_cast<std::size_t>(width) * height, 1) {
}

void Screen::put_pixel(int x, int y, std::uint8_t draw_mask, std::uint8_t color, std::uint8_t priority,
                       std::uint8_t control) {
    const std::size_t offset = index(x, y);
    if ((draw_mask & mask_visual) != 0) {
        visual_[offset] = color;
        cover_[offset] = 1;
    }
    if ((draw_mask & mask_priority) != 0) {
        priority_[offset] = priority;
    }
    if ((draw_mask & mask_control) != 0) {
        control_[offset] = control;
    }
}

std::uint8_t Screen::drawing_mask(std::uint8_t color, std::uint8_t priority, std::uint8_t control) {
    std::uint8_t flag = 0;
    if (color != 255) {
        flag |= mask_visual;
    }
    if (priority != 255) {
        flag |= mask_priority;
    }
    if (control != 255) {
        flag |= mask_control;
    }
    return flag;
}

void Screen::draw_line(GPoint start, GPoint end, std::uint8_t color, std::uint8_t priority, std::uint8_t control) {
    int left = std::clamp<int>(start.x, 0, width - 1);
    int top = std::clamp<int>(start.y, 0, height - 1);
    int right = std::clamp<int>(end.x, 0, width - 1);
    int bottom = std::clamp<int>(end.y, 0, height - 1);
    const std::uint8_t draw_mask = drawing_mask(color, priority, control);
    const auto put = [&](int x, int y) { put_pixel(x, y, draw_mask, color, priority, control); };
    if (top == bottom) {
        if (right < left) {
            std::swap(right, left);
        }
        for (int i = left; i <= right; ++i) {
            put(i, top);
        }
        return;
    }
    if (left == right) {
        if (top > bottom) {
            std::swap(top, bottom);
        }
        for (int i = top; i <= bottom; ++i) {
            put(left, i);
        }
        return;
    }
    int dy = bottom - top;
    int dx = right - left;
    const int step_y = dy < 0 ? -1 : 1;
    const int step_x = dx < 0 ? -1 : 1;
    dy = std::abs(dy) * 2;
    dx = std::abs(dx) * 2;
    put(left, top);
    put(right, bottom);
    if (dx > dy) {
        int fraction = dy - (dx / 2);
        while (left != right) {
            if (fraction >= 0) {
                top += step_y;
                fraction -= dx;
            }
            left += step_x;
            fraction += dy;
            put(left, top);
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
            put(left, top);
        }
    }
}

std::size_t Screen::bits_data_size(const Rect& rect, std::uint8_t mask) {
    std::size_t bytes = rect_bytes + 1;
    const auto pixels = static_cast<std::size_t>(rect.width()) * static_cast<std::size_t>(rect.height());
    if ((mask & mask_visual) != 0) {
        bytes += pixels * 2; // The visual and the display map.
    }
    if ((mask & mask_priority) != 0) {
        bytes += pixels;
    }
    if ((mask & mask_control) != 0) {
        bytes += pixels;
    }
    return bytes;
}

void Screen::bits_save(const Rect& rect, std::uint8_t mask, std::span<std::uint8_t> memory) const {
    write16(memory, 0, rect.top);
    write16(memory, 2, rect.left);
    write16(memory, 4, rect.bottom);
    write16(memory, 6, rect.right);
    memory[rect_bytes] = mask;
    std::size_t at = rect_bytes + 1;
    const auto save = [&](const std::vector<std::uint8_t>& map) {
        for (int y = rect.top; y < rect.bottom; ++y) {
            for (int x = rect.left; x < rect.right; ++x) {
                memory[at++] = map[index(x, y)];
            }
        }
    };
    if ((mask & mask_visual) != 0) {
        save(visual_);
        save(cover_); // In place of the display map.
    }
    if ((mask & mask_priority) != 0) {
        save(priority_);
    }
    if ((mask & mask_control) != 0) {
        save(control_);
    }
}

Rect Screen::bits_rect(std::span<const std::uint8_t> memory) {
    return Rect{
        .top = read16(memory, 0),
        .left = read16(memory, 2),
        .bottom = read16(memory, 4),
        .right = read16(memory, 6),
    };
}

void Screen::bits_restore(std::span<const std::uint8_t> memory) {
    const Rect rect = bits_rect(memory);
    const std::uint8_t mask = memory[rect_bytes];
    std::size_t at = rect_bytes + 1;
    const auto restore = [&](std::vector<std::uint8_t>* map) {
        for (int y = rect.top; y < rect.bottom; ++y) {
            for (int x = rect.left; x < rect.right; ++x) {
                const std::uint8_t value = memory[at++];
                if (map != nullptr) {
                    (*map)[index(x, y)] = value;
                }
            }
        }
    };
    if ((mask & mask_visual) != 0) {
        restore(&visual_);
        restore(&cover_); // In place of the display map.
    }
    if ((mask & mask_priority) != 0) {
        restore(&priority_);
    }
    if ((mask & mask_control) != 0) {
        restore(&control_);
    }
}

void Screen::show(const Rect& rect) {
    Rect clipped = rect;
    clipped.clip(Rect::from(0, 0, width, height));
    bool changed = false;
    for (int y = clipped.top; y < clipped.bottom; ++y) {
        for (int x = clipped.left; x < clipped.right; ++x) {
            const std::size_t i = index(x, y);
            changed = changed || shown_[i] != visual_[i] || shown_cover_[i] != cover_[i];
            shown_[i] = visual_[i];
            shown_cover_[i] = cover_[i];
        }
    }
    if (changed) {
        ++shown_serial_;
    }
}

void Screen::uncover_picture() {
    std::fill(cover_.begin() + static_cast<std::ptrdiff_t>(index(0, pic::picture_top)), cover_.end(), 0);
}

void Screen::cover_changes(std::span<const std::uint8_t> visual_before) {
    for (std::size_t i = 0; i < cover_.size() && i < visual_before.size(); ++i) {
        if (visual_[i] != visual_before[i]) {
            cover_[i] = 1;
        }
    }
}

std::int16_t Screen::kernel_pic_not_valid(std::int16_t new_value) {
    const auto old = static_cast<std::int16_t>(pic_not_valid_);
    if (new_value != -1) {
        pic_not_valid_ = new_value;
    }
    return old;
}

} // namespace sci::vm
