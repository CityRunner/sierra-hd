// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/text16.{h,cpp} (SCI0: no text codes, single-byte fonts)
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

#include "gfx_rect.hpp"

#include <cstddef>
#include <string_view>

namespace sci::vm {

class Graphics;
class GfxFont;

enum TextAlignment : int { text_align_right = -1, text_align_left = 0, text_align_center = 1 };

class Text {
public:
    explicit Text(Graphics& graphics) : graphics_(&graphics) {}

    [[nodiscard]] int font_id() const;
    const GfxFont& font();
    void set_font(int font_id);

    // The characters of the next line from `position`, which moves past it
    // and the spaces after a break.
    int get_longest(std::string_view text, std::size_t& position, int max_width);
    // Width and height of `length` characters from `from`.
    void width(std::string_view text, std::size_t from, int length, int& text_width, int& text_height,
               bool restore_font);

    void string_width(std::string_view text, int& text_width, int& text_height) {
        width(text, 0, static_cast<int>(text.size()), text_width, text_height, true);
    }

    // The rectangle `text` needs; a negative width forces one line.
    Rect size(std::string_view text, int font_id, int max_width);
    void draw(std::string_view text, std::size_t from, int length);
    void show(std::string_view text, std::size_t from, int length);
    void box(std::string_view text, bool show, const Rect& rect, int alignment, int font_id);
    void draw_string(std::string_view text);
    void draw_status(std::string_view text);
    void kernel_text_size(std::string_view text, int font_id, int max_width, int& text_width, int& text_height);

private:
    Graphics* graphics_;
    const GfxFont* font_ = nullptr;
};

} // namespace sci::vm
