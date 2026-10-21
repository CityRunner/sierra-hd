// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/cache.cpp, view.cpp (SCI0 EGA views), scifont.cpp
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

#include "gfx_cache.hpp"

#include "gfx_screen.hpp"
#include "machine.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace sci::vm {

int GfxView::clamp_loop(int loop) const {
    return std::clamp(loop, 0, loop_count() - 1);
}

int GfxView::cel_count(int loop) const {
    if (view_.loops.empty()) {
        return 0;
    }
    return static_cast<int>(view_.loops[static_cast<std::size_t>(clamp_loop(loop))].cels.size());
}

const view::Cel& GfxView::cel(int loop, int cel) const {
    static const view::Cel empty{};
    if (view_.loops.empty()) {
        return empty;
    }
    const view::Loop& found = view_.loops[static_cast<std::size_t>(clamp_loop(loop))];
    if (found.cels.empty()) {
        return empty;
    }
    return found.cels[static_cast<std::size_t>(std::clamp(cel, 0, static_cast<int>(found.cels.size()) - 1))];
}

Rect GfxView::cel_rect(int loop, int cel, int x, int y, int z) const {
    const view::Cel& info = this->cel(loop, cel);
    const int left = x + info.displace_x - (info.width / 2);
    const int bottom = y + info.displace_y - z + 1;
    return Rect::from(left, bottom - info.height, left + info.width, bottom);
}

void GfxView::draw(Screen& screen, const Rect& rect, const Rect& clip, const Rect& clip_translated, int loop, int cel,
                   std::uint8_t priority) const {
    const view::Cel& info = this->cel(loop, cel);
    const int cel_width = info.width;
    const int width = std::min(clip.width(), cel_width);
    const int height = std::min(clip.height(), static_cast<int>(info.height));
    if (width == 0 || height == 0) {
        return;
    }
    const std::uint8_t draw_mask = priority > 15 ? mask_visual : mask_visual | mask_priority;
    const auto start = static_cast<std::size_t>(((clip.top - rect.top) * cel_width) + (clip.left - rect.left));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t color = info.pixels[start + static_cast<std::size_t>((y * cel_width) + x)];
            if (color == info.clear_key) {
                continue;
            }
            const int x2 = clip_translated.left + x;
            const int y2 = clip_translated.top + y;
            if (priority >= screen.priority(x2, y2)) {
                screen.put_pixel(x2, y2, draw_mask, color, priority, 0);
            }
        }
    }
}

void GfxFont::draw(Screen& screen, std::uint16_t chr, int top, int left, std::uint8_t color, bool greyed) const {
    if (chr >= font_.glyphs.size()) {
        warn(std::format("font.{:03} is missing glyph {}", id_, chr));
        return;
    }
    const text::Glyph& glyph = font_.glyphs[chr];
    int greyed_top = top;
    for (int y = 0; std::cmp_less(y, glyph.height); ++y) {
        // Greyed text masks each row with 0x55 or 0xAA: odd or even columns.
        int keep_parity = -1;
        if (greyed) {
            keep_parity = (greyed_top++ % 2) != 0 ? 0 : 1;
        }
        for (int x = 0; std::cmp_less(x, glyph.width); ++x) {
            if (glyph.pixels[static_cast<std::size_t>((y * glyph.width) + x)] == 0 ||
                (keep_parity >= 0 && (x % 2) != keep_parity)) {
                continue;
            }
            const int screen_x = left + x;
            const int screen_y = top + y;
            if (screen_x >= 0 && screen_x < Screen::width && screen_y >= 0 && screen_y < Screen::height) {
                screen.put_pixel(screen_x, screen_y, mask_visual, color, 0, 0);
            } else {
                warn(std::format("font.{:03} glyph {} drawn out of bounds: {}, {}", id_, chr, screen_x, screen_y));
            }
        }
    }
}

const GfxView& GfxCache::view(int id) {
    if (const auto found = views_.find(id); found != views_.end()) {
        return found->second;
    }
    view::View decoded;
    const auto* data = machine_->resources().find(ResourceType::View, id);
    if (data == nullptr) {
        machine_->faults().raise(std::format("view resource {} not found", id));
    } else if (auto parsed = view::decode_view(*data)) {
        decoded = std::move(*parsed);
    } else {
        machine_->faults().raise(std::format("view.{:03}: {}", id, parsed.error().message));
    }
    return views_.emplace(id, GfxView(std::move(decoded))).first->second;
}

const GfxFont& GfxCache::font(int id) {
    if (const auto found = fonts_.find(id); found != fonts_.end()) {
        return found->second;
    }
    int number = id;
    if (!machine_->resources().contains(
            ResourceId{.type = ResourceType::Font, .number = static_cast<std::uint16_t>(id)})) {
        number = static_cast<int>(static_cast<unsigned>(id) & 0x7FFU);
    }
    text::Font decoded;
    const auto* data = machine_->resources().find(ResourceType::Font, number);
    if (data == nullptr) {
        machine_->faults().raise(std::format("font resource {} not found", number));
    } else if (auto parsed = text::decode_font(*data)) {
        decoded = std::move(*parsed);
    } else {
        machine_->faults().raise(std::format("font.{:03}: {}", number, parsed.error().message));
    }
    return fonts_.emplace(id, GfxFont(id, std::move(decoded))).first->second;
}

} // namespace sci::vm
