// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/controls16.{h,cpp} and engine/kgraphics.cpp
//   (_k_GenericDrawControl, kDrawControl, kHiliteControl, kEditControl)
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

#include "gfx_controls.hpp"

#include "gfx_layout.hpp"
#include "graphics.hpp"
#include "machine.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace sci::vm {

namespace {

constexpr std::uint16_t key_etx = 3;
constexpr std::uint16_t key_backspace = 8;
constexpr std::uint16_t key_home = 71U << 8U;
constexpr std::uint16_t key_end = 79U << 8U;
constexpr std::uint16_t key_delete = 83U << 8U;

// kControlCreateRect: an empty rectangle where right or bottom are short.
Rect control_rect(int x, int y, int x1, int y1) {
    return Rect::from(x, y, std::max(x, x1), std::max(y, y1));
}

// A loop or cel number read as a signed byte.
int signed_byte(int value) {
    return (static_cast<unsigned>(value) & 0x80U) != 0 ? value - 256 : value;
}

} // namespace

bool Controls::pic_not_valid() const {
    return graphics_->screen().pic_not_valid() != 0;
}

void Controls::invert(const Rect& rect) {
    // SCI0 late on DOS inverts with the pen and background colours.
    graphics_->paint().invert_rect(rect);
    graphics_->paint().bits_show(rect);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to _k_GenericDrawControl.
void Controls::draw_control(Reg control, bool hilite) {
    Machine& machine = graphics_->machine();
    const SegManager& segments = machine.segments();
    const SelectorCache& s = machine.selector();
    const int type = machine.read_selector_value(control, s.type);
    const auto style = static_cast<unsigned>(static_cast<std::uint16_t>(machine.read_selector_value(control, s.state)));
    const int x = machine.read_selector_value(control, s.ns_left);
    const int y = machine.read_selector_value(control, s.ns_top);
    const int font_id = machine.read_selector_value(control, s.font);
    const Reg text_pointer = machine.read_selector(control, s.text);
    const Rect rect = control_rect(x, y, machine.read_selector_value(control, s.ns_right),
                                   machine.read_selector_value(control, s.ns_bottom));
    std::string text;
    if (!text_pointer.is_null()) {
        text = segments.get_string(text_pointer);
    }
    if (graphics_->layout_logging()) {
        graphics_->log_layout(std::format("control {} {:04x}:{:04x} rect={} state={:04x} font={} hilite={}", type,
                                          control.segment, control.offset, layout_rect(rect, graphics_->ports().port()),
                                          style, font_id, hilite ? 1 : 0));
    }
    switch (type) {
    case control_type_button:
        draw_button(rect, graphics_->str_split(text, nullptr), font_id, style, hilite);
        return;
    case control_type_text:
        draw_text(rect, graphics_->str_split(text, "\r----------\r"), font_id,
                  machine.read_selector_value(control, s.mode), style, hilite);
        // The rectangles of |r| text codes: none in SCI0.
        machine.set_acc(null_reg);
        return;
    case control_type_textedit: {
        int cursor = machine.read_selector_value(control, s.cursor);
        if (std::cmp_greater(cursor, text.size())) {
            cursor = static_cast<int>(text.size());
            machine.write_selector_value(control, s.cursor, cursor);
        }
        draw_textedit(rect, graphics_->str_split(text, nullptr), font_id, style, cursor, hilite);
        return;
    }
    case control_type_icon: {
        const int view = machine.read_selector_value(control, s.view);
        const int loop = signed_byte(machine.read_selector_value(control, s.loop));
        const int cel = signed_byte(machine.read_selector_value(control, s.cel));
        int priority = -1;
        if (machine.lookup_selector(control, s.priority, nullptr, nullptr, nullptr) ==
            Machine::SelectorKind::Variable) {
            priority = machine.read_selector_value(control, s.priority);
        }
        draw_icon(rect, view, loop, cel, priority, style, hilite);
        return;
    }
    case control_type_list:
    case control_type_list_alias: {
        const bool is_alias = type == control_type_list_alias;
        const int max_chars = machine.read_selector_value(control, s.x); // Characters per entry.
        const auto cursor_offset = static_cast<std::uint16_t>(machine.read_selector_value(control, s.cursor));
        std::uint16_t upper_offset = 0;
        if (s.top_string != no_selector) {
            upper_offset = static_cast<std::uint16_t>(machine.read_selector_value(control, s.top_string));
        } else if (machine.lookup_selector(control, s.br_top, nullptr, nullptr, nullptr) ==
                   Machine::SelectorKind::Variable) {
            upper_offset = static_cast<std::uint16_t>(machine.read_selector_value(control, s.br_top));
        } else {
            upper_offset = static_cast<std::uint16_t>(machine.read_selector_value(control, s.ls_top));
        }
        // The entries: strings of max_chars bytes, up to an empty one.
        std::vector<std::string> entries;
        int upper_position = 0;
        int cursor_position = 0;
        Reg seeker = text_pointer;
        while (segments.strlen(seeker) > 0) {
            if (seeker.offset == upper_offset) {
                upper_position = static_cast<int>(entries.size());
            }
            if (seeker.offset == cursor_offset) {
                cursor_position = static_cast<int>(entries.size());
            }
            entries.push_back(segments.get_string(seeker));
            seeker.offset = static_cast<std::uint16_t>(seeker.offset + max_chars);
        }
        draw_list(rect, max_chars, entries, font_id, style, upper_position, cursor_position, is_alias, hilite);
        return;
    }
    case control_type_dummy:
        return; // QfG1 EGA uses it; it draws nothing.
    default:
        machine.faults().raise(std::format("unsupported control type {}", type));
        return;
    }
}

void Controls::draw_button(Rect rect, const std::string& text, int font_id, unsigned style, bool hilite) {
    if (hilite) {
        invert(rect);
        return;
    }
    Paint& paint = graphics_->paint();
    Ports& ports = graphics_->ports();
    rect.grow(1);
    paint.erase_rect(rect);
    paint.frame_rect(rect);
    rect.grow(-2);
    ports.text_greyed_output((style & control_style_enabled) == 0);
    graphics_->text().box(text, false, rect, text_align_center, font_id);
    ports.text_greyed_output(false);
    rect.grow(1);
    if ((style & control_style_selected) != 0) {
        paint.frame_rect(rect);
    }
    if (!pic_not_valid()) {
        rect.grow(1);
        paint.bits_show(rect);
    }
}

void Controls::draw_text(Rect rect, const std::string& text, int font_id, int alignment, unsigned style, bool hilite) {
    if (hilite) {
        invert(rect);
        return;
    }
    Paint& paint = graphics_->paint();
    rect.grow(1);
    paint.erase_rect(rect);
    rect.grow(-1);
    graphics_->text().box(text, false, rect, alignment, font_id);
    if ((style & control_style_selected) != 0) {
        paint.frame_rect(rect);
    }
    if (!pic_not_valid()) {
        paint.bits_show(rect);
    }
}

void Controls::draw_textedit(Rect rect, const std::string& text, int font_id, unsigned style, int cursor_position,
                             bool /*hilite*/) {
    Paint& paint = graphics_->paint();
    Text& text16 = graphics_->text();
    const Rect text_rect = rect;
    const int old_font = text16.font_id();
    rect.grow(1);
    cursor_visible_ = false;
    textedit_cursor_erase();
    paint.erase_rect(rect);
    text16.box(text, false, text_rect, text_align_left, font_id);
    paint.frame_rect(rect);
    if ((style & control_style_selected) != 0) {
        text16.set_font(font_id);
        rect.grow(-1);
        textedit_cursor_draw(rect, text, static_cast<std::size_t>(std::max(cursor_position, 0)));
        text16.set_font(old_font);
        rect.grow(1);
    }
    if (!pic_not_valid()) {
        paint.bits_show(rect);
    }
}

void Controls::draw_icon(Rect rect, int view, int loop, int cel, int priority, unsigned style, bool hilite) {
    if (hilite) {
        invert(rect);
        return;
    }
    Paint& paint = graphics_->paint();
    paint.draw_cel_and_show(view, loop, cel, rect.left, rect.top, priority, 0);
    if ((style & 0x20U) != 0) {
        paint.frame_rect(rect);
    }
    if (!pic_not_valid()) {
        paint.bits_show(rect);
    }
}

void Controls::draw_list(Rect rect, int max_chars, const std::vector<std::string>& entries, int font_id, unsigned style,
                         int upper_position, int cursor_position, bool is_alias, bool hilite) {
    if (hilite) {
        return; // ScummVM draws nothing for a selected list.
    }
    Paint& paint = graphics_->paint();
    draw_list_control(rect, max_chars, entries, font_id, upper_position, cursor_position, is_alias);
    rect.grow(1);
    if (is_alias && (style & control_style_selected) != 0) {
        paint.frame_rect(rect);
    }
    if (!pic_not_valid()) {
        paint.bits_show(rect);
    }
}

void Controls::draw_list_control(Rect rect, int max_chars, const std::vector<std::string>& entries, int font_id,
                                 int upper_position, int cursor_position, bool is_alias) {
    Paint& paint = graphics_->paint();
    Ports& ports = graphics_->ports();
    Text& text16 = graphics_->text();
    const int old_font = text16.font_id();
    Rect worker = rect;
    paint.erase_rect(worker);
    worker.grow(1);
    paint.frame_rect(worker);
    // The arrows up and down, in font 0.
    ++worker.top;
    text16.box("\x18", false, worker, text_align_center, 0);
    worker.top = static_cast<std::int16_t>(worker.bottom - 10);
    text16.box("\x19", false, worker, text_align_center, 0);
    worker.top = static_cast<std::int16_t>(rect.top + 9);
    worker.bottom = static_cast<std::int16_t>(worker.bottom - 10);
    paint.frame_rect(worker);
    worker.grow(-1);

    text16.set_font(font_id);
    const int font_size = ports.port()->font_height;
    worker.bottom = static_cast<std::int16_t>(worker.top + font_size);
    const int last_y = rect.bottom - font_size;
    for (int i = upper_position; std::cmp_less(i, entries.size()); ++i) {
        paint.erase_rect(worker);
        const std::string& entry = entries[static_cast<std::size_t>(i)];
        if (!entry.empty()) {
            ports.move_to(worker.left, worker.top);
            text16.draw(entry, 0, std::min(max_chars, static_cast<int>(entry.size())));
            if (!is_alias && i == cursor_position) {
                paint.invert_rect(worker);
            }
        }
        worker.translate(0, font_size);
        if (worker.bottom > last_y) {
            break;
        }
    }
    text16.set_font(old_font);
}

void Controls::textedit_cursor_draw(const Rect& rect, const std::string& text, std::size_t position) {
    if (cursor_visible_) {
        return;
    }
    const GfxFont& font = graphics_->text().font();
    int text_width = 0;
    for (std::size_t i = 0; i < position && i < text.size(); ++i) {
        text_width += font.char_width(static_cast<unsigned char>(text[i]));
    }
    cursor_rect_.left = static_cast<std::int16_t>(rect.left + text_width);
    cursor_rect_.top = rect.top;
    cursor_rect_.bottom = static_cast<std::int16_t>(cursor_rect_.top + font.height());
    const int width = position < text.size() ? font.char_width(static_cast<unsigned char>(text[position])) : 1;
    cursor_rect_.right = static_cast<std::int16_t>(cursor_rect_.left + width);
    graphics_->paint().invert_rect(cursor_rect_);
    graphics_->paint().bits_show(cursor_rect_);
    cursor_visible_ = true;
    textedit_set_blink_time();
}

void Controls::textedit_cursor_erase() {
    if (cursor_visible_) {
        graphics_->paint().invert_rect(cursor_rect_);
        graphics_->paint().bits_show(cursor_rect_);
        cursor_visible_ = false;
    }
    textedit_set_blink_time();
}

// The cursor blinks every 30 ticks. ScummVM times it on the host clock; it
// changes only the look of the field, and here it follows the virtual clock.
void Controls::textedit_set_blink_time() {
    blink_time_ = graphics_->machine().subsystems().clock().millis() + (30U * 1000U / 60U);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxControls16::kernelTexteditChange.
void Controls::kernel_textedit_change(Reg control, Reg event) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    Paint& paint = graphics_->paint();
    Text& text16 = graphics_->text();
    auto cursor = static_cast<std::uint16_t>(machine.read_selector_value(control, s.cursor));
    const auto max_chars = static_cast<std::uint16_t>(machine.read_selector_value(control, s.max));
    const Reg text_pointer = machine.read_selector(control, s.text);
    if (text_pointer.is_null()) {
        machine.faults().raise("kEditControl called on object that doesn't have a text reference");
        return;
    }
    std::string text = machine.segments().get_string(text_pointer);
    std::uint16_t key = 0;
    bool changed = false;
    bool add_char = false;
    if (!event.is_null()) {
        const std::size_t size = text.size();
        const auto type = static_cast<std::uint16_t>(machine.read_selector_value(event, s.type));
        if (type == event_key_down) {
            key = static_cast<std::uint16_t>(machine.read_selector_value(event, s.message));
            const auto modifiers = static_cast<std::uint16_t>(machine.read_selector_value(event, s.modifiers));
            const bool ctrl = (modifiers & modifier_ctrl) != 0;
            switch (key) {
            case key_backspace:
                if (cursor > 0) {
                    --cursor;
                    text.erase(cursor, 1);
                    changed = true;
                }
                break;
            case key_delete:
                if (cursor < size) {
                    text.erase(cursor, 1);
                    changed = true;
                }
                break;
            case key_home:
                cursor = 0;
                changed = true;
                break;
            case key_end:
                cursor = static_cast<std::uint16_t>(size);
                changed = true;
                break;
            case key_left:
                if (cursor > 0) {
                    --cursor;
                    changed = true;
                }
                break;
            case key_right:
                if (cursor + 1U <= size) {
                    ++cursor;
                    changed = true;
                }
                break;
            case key_etx:
                if (ctrl) {
                    cursor = 0;
                    text.clear();
                    changed = true;
                }
                break;
            default:
                if (ctrl && key == 99) {
                    cursor = 0;
                    text.clear();
                    changed = true;
                } else if (key > 31 && key < 256 && size < max_chars) {
                    add_char = true;
                    changed = true;
                }
                break;
            }
        }
    }
    // ScummVM's alternate input (vocabulary 913) does not apply: the game
    // has no such vocabulary.
    if (changed) {
        const int old_font = text16.font_id();
        const int font_id = machine.read_selector_value(control, s.font);
        const Rect rect = graphics_->compare().ns_rect(control);
        text16.set_font(font_id);
        if (add_char) {
            const GfxFont& font = text16.font();
            int text_width = 0;
            for (const char c : text) {
                text_width += font.char_width(static_cast<unsigned char>(c));
            }
            text_width += font.char_width(key);
            if (text_width >= rect.width()) {
                // No room: nothing changes, not even the cursor selector.
                text16.set_font(old_font);
                return;
            }
            text.insert(cursor, 1, static_cast<char>(key));
            ++cursor;
        }
        textedit_cursor_erase();
        paint.erase_rect(rect);
        text16.box(text, false, rect, text_align_left, -1);
        paint.bits_show(rect);
        textedit_cursor_draw(rect, text, cursor);
        text16.set_font(old_font);
        machine.segments().strcpy(text_pointer, text);
    } else if (machine.subsystems().clock().millis() >= blink_time_) {
        paint.invert_rect(cursor_rect_);
        paint.bits_show(cursor_rect_);
        cursor_visible_ = !cursor_visible_;
        textedit_set_blink_time();
    }
    machine.write_selector_value(control, s.cursor, cursor);
}

} // namespace sci::vm
