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
constexpr std::size_t pixel_count = static_cast<std::size_t>(Screen::width) * Screen::height;

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
    : visual_(pixel_count, 0), priority_(pixel_count, 0), control_(pixel_count, 0),
      owner_(pixel_count, static_cast<std::uint8_t>(Owner::Interface)), source_(pixel_count, priority_from_drawing),
      bg_visual_(pixel_count, 0), bg_owner_(pixel_count, 0), bg_priority_(pixel_count, 0), bg_source_(pixel_count, 0),
      shown_(pixel_count, 0), shown_owner_(pixel_count, static_cast<std::uint8_t>(Owner::Interface)),
      shown_bg_visual_(pixel_count, 0), shown_bg_owner_(pixel_count, static_cast<std::uint8_t>(Owner::Interface)),
      shown_bg_priority_(pixel_count, 0), shown_bg_source_(pixel_count, priority_from_drawing),
      shown_layer_(pixel_count, 0) {
}

void Screen::put_pixel(int x, int y, std::uint8_t draw_mask, std::uint8_t color, std::uint8_t priority,
                       std::uint8_t control) {
    const std::size_t offset = index(x, y);
    if ((draw_mask & mask_visual) != 0) {
        visual_[offset] = color;
        owner_[offset] = static_cast<std::uint8_t>(current_owner_);
    }
    if ((draw_mask & mask_priority) != 0) {
        priority_[offset] = priority;
        source_[offset] = priority_from_drawing;
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
        bytes += pixels * 2; // The priority and the priority source map.
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
        save(owner_); // In place of the display map.
    }
    if ((mask & mask_priority) != 0) {
        save(priority_);
        save(source_);
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
        restore(&owner_); // In place of the display map.
    }
    if ((mask & mask_priority) != 0) {
        restore(&priority_);
        restore(&source_);
    }
    if ((mask & mask_control) != 0) {
        restore(&control_);
    }
}

void Screen::clear_for_restore() {
    std::ranges::fill(visual_, 0);
    std::ranges::fill(priority_, 0);
    std::ranges::fill(control_, 0);
    std::ranges::fill(owner_, static_cast<std::uint8_t>(Owner::Interface));
    std::ranges::fill(source_, priority_from_drawing);
    show(Rect{.top = 0, .left = 0, .bottom = height, .right = width});
}

void Screen::show(const Rect& rect) {
    Rect clipped = rect;
    clipped.clip(Rect{.top = 0, .left = 0, .bottom = height, .right = width});
    if (clipped.is_empty()) {
        return;
    }
    const std::uint32_t generation = drawing_sprites_ ? promote_sprites() : 0;
    const std::vector<std::uint8_t>& bg_visual = drawing_sprites_ ? bg_visual_ : visual_;
    const std::vector<std::uint8_t>& bg_owner = drawing_sprites_ ? bg_owner_ : owner_;
    const std::vector<std::uint8_t>& bg_priority = drawing_sprites_ ? bg_priority_ : priority_;
    const std::vector<std::uint8_t>& bg_source = drawing_sprites_ ? bg_source_ : source_;
    bool changed = false;
    const auto copy = [&changed](std::vector<std::uint8_t>& to, const std::vector<std::uint8_t>& from, std::size_t i) {
        changed = changed || to[i] != from[i];
        to[i] = from[i];
    };
    for (int y = clipped.top; y < clipped.bottom; ++y) {
        for (int x = clipped.left; x < clipped.right; ++x) {
            const std::size_t i = index(x, y);
            copy(shown_, visual_, i);
            copy(shown_owner_, owner_, i);
            copy(shown_bg_visual_, bg_visual, i);
            copy(shown_bg_owner_, bg_owner, i);
            copy(shown_bg_priority_, bg_priority, i);
            copy(shown_bg_source_, bg_source, i);
            changed = changed || shown_layer_[i] != generation;
            shown_layer_[i] = generation;
        }
    }
    if (changed) {
        ++shown_serial_;
    }
}

void Screen::picture_drawn() {
    const auto first = static_cast<std::ptrdiff_t>(index(0, pic::picture_top));
    std::fill(owner_.begin() + first, owner_.end(), static_cast<std::uint8_t>(Owner::Picture));
    std::fill(source_.begin() + first, source_.end(), priority_from_picture);
}

void Screen::overlay_drawn(std::span<const std::uint8_t> visual_before, std::span<const std::uint8_t> priority_before) {
    for (std::size_t i = 0; i < owner_.size() && i < visual_before.size() && i < priority_before.size(); ++i) {
        if (visual_[i] != visual_before[i]) {
            owner_[i] = static_cast<std::uint8_t>(Owner::Picture);
        }
        if (priority_[i] != priority_before[i]) {
            source_[i] = priority_from_picture;
        }
    }
}

void Screen::begin_sprites() {
    bg_visual_ = visual_;
    bg_owner_ = owner_;
    bg_priority_ = priority_;
    bg_source_ = source_;
    pending_sprites_.clear();
    pending_generation_.reset();
    drawing_sprites_ = true;
}

void Screen::add_sprite(const ShownSprite& sprite) {
    pending_sprites_.push_back(sprite);
    // A list already shown changes: the rest is shown with a new one.
    pending_generation_.reset();
}

void Screen::end_sprites() {
    drawing_sprites_ = false;
    pending_sprites_.clear();
    pending_generation_.reset();
}

std::uint32_t Screen::promote_sprites() {
    if (pending_generation_) {
        return *pending_generation_;
    }
    if (pending_sprites_.empty()) {
        pending_generation_ = 0;
        return 0;
    }
    // The same sprites as the last list: shown pixels need not change.
    if (!sprite_lists_.empty() && sprite_lists_.back().sprites == pending_sprites_) {
        pending_generation_ = sprite_lists_.back().generation;
        return *pending_generation_;
    }
    // Lists no shown pixel names any more are dropped.
    std::vector<std::uint32_t> named;
    for (const std::uint32_t generation : shown_layer_) {
        if (generation != 0 && (named.empty() || named.back() != generation) &&
            std::ranges::find(named, generation) == named.end()) {
            named.push_back(generation);
        }
    }
    std::erase_if(sprite_lists_,
                  [&](const SpriteList& list) { return std::ranges::find(named, list.generation) == named.end(); });
    pending_generation_ = next_generation_++;
    sprite_lists_.push_back(SpriteList{.generation = *pending_generation_, .sprites = pending_sprites_});
    return *pending_generation_;
}

std::int16_t Screen::kernel_pic_not_valid(std::int16_t new_value) {
    const auto old = static_cast<std::int16_t>(pic_not_valid_);
    if (new_value != -1) {
        pic_not_valid_ = new_value;
    }
    return old;
}

} // namespace sci::vm
