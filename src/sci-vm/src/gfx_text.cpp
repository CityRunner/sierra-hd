// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/text16.cpp (SCI0: no text codes, single-byte fonts)
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

#include "gfx_text.hpp"

#include "faults.hpp"
#include "graphics.hpp"

#include <algorithm>

namespace sci::vm {

namespace {

std::uint16_t char_at(std::string_view text, std::size_t position) {
    return position < text.size() ? static_cast<std::uint8_t>(text[position]) : 0;
}

} // namespace

int Text::font_id() const {
    return graphics_->ports().port()->font_id;
}

const GfxFont& Text::font() {
    if (font_ == nullptr || font_->id() != font_id()) {
        font_ = &graphics_->cache().font(font_id());
    }
    return *font_;
}

void Text::set_font(int font_id) {
    if (font_ == nullptr || font_->id() != font_id) {
        font_ = &graphics_->cache().font(font_id);
    }
    Port* port = graphics_->ports().port();
    port->font_id = font_->id();
    port->font_height = static_cast<std::int16_t>(font_->height());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxText16::GetLongest.
int Text::get_longest(std::string_view text, std::size_t& position, int max_width) {
    Ports& ports = graphics_->ports();
    const int previous_font = font_id();
    const int previous_pen = ports.port()->pen_color;
    const GfxFont& current = font();
    std::size_t last_space_position = 0;
    int last_space_count = 0;
    int count = 0;
    int temp_width = 0;
    const auto restore = [&] {
        set_font(previous_font);
        ports.pen_color(previous_pen);
    };
    for (;;) {
        const std::uint16_t chr = char_at(text, position);
        bool stop = false;
        switch (chr) {
        case 0x0D:
            if (char_at(text, position + 1) == 0x0A) {
                ++count;
                ++position;
            }
            ++count;
            ++position;
            stop = true;
            break;
        case 0x0A:
            ++count;
            ++position;
            stop = true;
            break;
        case 0:
            stop = true;
            break;
        case ' ':
            last_space_count = count;
            last_space_position = position + 1;
            break;
        default:
            break;
        }
        if (stop) {
            restore();
            return count;
        }
        temp_width += current.char_width(chr);
        if (temp_width > max_width) {
            break;
        }
        // SCI0 broke one character earlier while no space had been seen.
        if (last_space_count == 0 && temp_width == max_width) {
            break;
        }
        ++count;
        ++position;
    }
    int result = 0;
    if (last_space_count != 0) {
        result = last_space_count;
        position = last_space_position;
        while (char_at(text, position) == ' ') {
            ++position;
        }
    } else {
        // No space: the word is split, counting the character that did not
        // fit (useEarlyGetLongestTextCalculations).
        ++count;
        ++position;
        result = count;
    }
    restore();
    return result;
}

void Text::width(std::string_view text, std::size_t from, int length, int& text_width, int& text_height,
                 bool restore_font) {
    Ports& ports = graphics_->ports();
    const int previous_font = font_id();
    const int previous_pen = ports.port()->pen_color;
    text_width = 0;
    text_height = 0;
    const GfxFont& current = font();
    std::size_t position = from;
    while (length-- > 0) {
        const std::uint16_t chr = char_at(text, position++);
        text_height = std::max<int>(text_height, ports.port()->font_height);
        if (chr != 0x0A && chr != 0x0D) {
            text_width += current.char_width(chr);
        }
    }
    if (restore_font) {
        set_font(previous_font);
        ports.pen_color(previous_pen);
    }
}

Rect Text::size(std::string_view text, int font_id, int max_width) {
    Ports& ports = graphics_->ports();
    const int previous_font = this->font_id();
    const int previous_pen = ports.port()->pen_color;
    if (font_id != -1) {
        set_font(font_id);
    }
    Rect rect;
    int text_width = 0;
    int text_height = 0;
    if (max_width < 0) {
        string_width(text, text_width, text_height);
        rect.bottom = static_cast<std::int16_t>(text_height);
        rect.right = static_cast<std::int16_t>(text_width);
    } else {
        rect.right = static_cast<std::int16_t>(max_width != 0 ? max_width : 192);
        std::size_t position = 0;
        std::size_t line = 0;
        int max_text_width = 0;
        int total_height = 0;
        while (char_at(text, position) != 0) {
            const int count = get_longest(text, position, rect.right);
            if (count == 0) {
                break;
            }
            width(text, line, count, text_width, text_height, false);
            max_text_width = std::max(text_width, max_text_width);
            total_height += text_height;
            line = position;
        }
        rect.bottom = static_cast<std::int16_t>(total_height);
        rect.right = static_cast<std::int16_t>(max_width != 0 ? max_width : std::min<int>(rect.right, max_text_width));
    }
    set_font(previous_font);
    ports.pen_color(previous_pen);
    return rect;
}

void Text::draw(std::string_view text, std::size_t from, int length) {
    const GfxFont& current = font();
    Port* port = graphics_->ports().port();
    Rect rect;
    rect.top = port->cur_top;
    rect.bottom = static_cast<std::int16_t>(rect.top + port->font_height);
    std::size_t position = from;
    while (length-- > 0) {
        const std::uint16_t chr = char_at(text, position++);
        if (chr == 0x0A || chr == 0x0D || chr == 0) {
            continue;
        }
        const int char_width = current.char_width(chr);
        if (port->pen_mode == 1) {
            rect.left = port->cur_left;
            rect.right = static_cast<std::int16_t>(rect.left + char_width);
            graphics_->paint().erase_rect(rect);
        }
        current.draw(graphics_->screen(), chr, port->top + port->cur_top, port->left + port->cur_left,
                     static_cast<std::uint8_t>(port->pen_color), port->greyed_output);
        port->cur_left = static_cast<std::int16_t>(port->cur_left + char_width);
    }
}

void Text::show(std::string_view text, std::size_t from, int length) {
    Ports& ports = graphics_->ports();
    Rect rect;
    rect.top = ports.port()->cur_top;
    rect.bottom = static_cast<std::int16_t>(rect.top + ports.point_size());
    rect.left = ports.port()->cur_left;
    draw(text, from, length);
    rect.right = ports.port()->cur_left;
    graphics_->paint().bits_show(rect);
}

void Text::box(std::string_view text, bool show, const Rect& rect, int alignment, int font_id) {
    Ports& ports = graphics_->ports();
    const int previous_font = this->font_id();
    const int previous_pen = ports.port()->pen_color;
    if (font_id != -1) {
        set_font(font_id);
    }
    int text_width = 0;
    int text_height = 0;
    int hline = 0;
    std::size_t position = 0;
    std::size_t line = 0;
    while (char_at(text, position) != 0) {
        const int count = get_longest(text, position, rect.width());
        if (count == 0) {
            break;
        }
        width(text, line, count, text_width, text_height, true);
        int offset = 0;
        switch (alignment) {
        case text_align_right:
            offset = rect.width() - text_width;
            break;
        case text_align_center:
            offset = (rect.width() - text_width) / 2;
            break;
        case text_align_left:
            offset = 0;
            break;
        default:
            warn("Invalid alignment used in TextBox()");
            break;
        }
        ports.move_to(rect.left + offset, rect.top + hline);
        if (show) {
            this->show(text, line, count);
        } else {
            draw(text, line, count);
        }
        hline += text_height;
        line = position;
    }
    set_font(previous_font);
    ports.pen_color(previous_pen);
}

void Text::draw_string(std::string_view text) {
    Ports& ports = graphics_->ports();
    const int previous_font = font_id();
    const int previous_pen = ports.port()->pen_color;
    draw(text, 0, static_cast<int>(text.size()));
    set_font(previous_font);
    ports.pen_color(previous_pen);
}

void Text::draw_status(std::string_view text) {
    const GfxFont& current = font();
    Port* port = graphics_->ports().port();
    for (const char c : text) {
        const auto chr = static_cast<std::uint16_t>(static_cast<std::uint8_t>(c));
        if (chr == 0) {
            continue;
        }
        const int char_width = current.char_width(chr);
        current.draw(graphics_->screen(), chr, port->top + port->cur_top, port->left + port->cur_left,
                     static_cast<std::uint8_t>(port->pen_color), port->greyed_output);
        port->cur_left = static_cast<std::int16_t>(port->cur_left + char_width);
    }
}

void Text::kernel_text_size(std::string_view text, int font_id, int max_width, int& text_width, int& text_height) {
    const Rect rect = size(text, font_id, max_width);
    text_width = rect.width();
    text_height = rect.height();
}

} // namespace sci::vm
