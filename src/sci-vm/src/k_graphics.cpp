// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kgraphics.cpp, kmenu.cpp (SCI0 functions)
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

#include "graphics.hpp"
#include "kernel_functions.hpp"
#include "machine.hpp"
#include "subsystems.hpp"
#include "text_lookup.hpp"
#include "timing.hpp"

#include <format>
#include <utility>

namespace sci::vm {

namespace {

Graphics& gfx(Machine& machine) {
    return machine.subsystems().graphics();
}

// EGA games may pass colours above 15: only the low nibble counted.
int adjust_graph_color(int color) {
    return color & 0x0F;
}

Rect graph_rect(std::span<Reg> args) {
    int x = args[1].to_sint16();
    int y = args[0].to_sint16();
    int x1 = args[3].to_sint16();
    int y1 = args[2].to_sint16();
    if (x > x1) {
        std::swap(x, x1);
    }
    if (y > y1) {
        std::swap(y, y1);
    }
    return Rect::from(x, y, x1, y1);
}

GPoint graph_point(std::span<Reg> args, std::size_t at) {
    return GPoint{.x = args[at + 1].to_sint16(), .y = args[at].to_sint16()};
}

Reg arg_or(std::span<Reg> args, std::size_t at, Reg fallback) {
    return at < args.size() ? args[at] : fallback;
}

} // namespace

void dir_loop_worker(Machine& machine, Reg object, std::uint16_t angle) {
    const SelectorCache& s = machine.selector();
    const int view = machine.read_selector_value(object, s.view);
    const auto signal = static_cast<std::uint16_t>(machine.read_selector_value(object, s.signal));
    if ((signal & signal_doesnt_turn) != 0) {
        return;
    }
    int use_loop = -1;
    if (angle > 315 || angle < 45) {
        use_loop = 3;
    } else if (angle > 135 && angle < 225) {
        use_loop = 2;
    }
    if (use_loop == -1) {
        use_loop = angle >= 180 ? 1 : 0;
    } else if (gfx(machine).cache().view(view).loop_count() < 4) {
        return;
    }
    machine.write_selector_value(object, s.loop, use_loop);
}

Reg k_set_cursor(Machine& machine, std::span<Reg> args) {
    // SCI0 (detectSetCursorType: early SCI0 in this game).
    Cursor& cursor = gfx(machine).cursor();
    int cursor_id = args[0].to_sint16();
    if (args.size() >= 4) {
        cursor.kernel_set_pos(GPoint{.x = args[2].to_sint16(), .y = args[3].to_sint16()});
    }
    if (args.size() >= 2 && args[1].to_sint16() == 0) {
        cursor_id = -1;
    }
    cursor.kernel_set_shape(cursor_id);
    return machine.acc();
}

Reg k_pic_not_valid(Machine& machine, std::span<Reg> args) {
    const std::int16_t value = !args.empty() ? static_cast<std::int16_t>(args[0].to_uint16()) : std::int16_t{-1};
    return number(static_cast<std::uint16_t>(gfx(machine).screen().kernel_pic_not_valid(value)));
}

Reg k_graph(Machine& machine, std::span<Reg> /*args*/) {
    machine.faults().raise("kGraph: not supposed to call this");
    return null_reg;
}

Reg k_graph_get_color_count(Machine& /*machine*/, std::span<Reg> /*args*/) {
    return number(16); // The EGA driver.
}

Reg k_graph_draw_line(Machine& machine, std::span<Reg> args) {
    const int color = adjust_graph_color(args[4].to_sint16());
    const int priority = args.size() > 5 ? args[5].to_sint16() : -1;
    const int control = args.size() > 6 ? args[6].to_sint16() : -1;
    gfx(machine).paint().kernel_graph_draw_line(graph_point(args, 0), graph_point(args, 2), color, priority, control);
    return machine.acc();
}

Reg k_graph_save_box(Machine& machine, std::span<Reg> args) {
    const auto mask = static_cast<std::uint8_t>(args[4].to_uint16() & mask_all);
    return gfx(machine).paint().bits_save(graph_rect(args), mask);
}

Reg k_graph_restore_box(Machine& machine, std::span<Reg> args) {
    gfx(machine).paint().bits_restore(args[0]);
    return machine.acc();
}

Reg k_graph_fill_box_background(Machine& machine, std::span<Reg> args) {
    gfx(machine).paint().erase_rect(graph_rect(args));
    return machine.acc();
}

Reg k_graph_fill_box_foreground(Machine& machine, std::span<Reg> args) {
    gfx(machine).paint().paint_rect(graph_rect(args));
    return machine.acc();
}

Reg k_graph_fill_box_any(Machine& machine, std::span<Reg> args) {
    // Priority and control may be read from beyond the arguments, as Sierra did.
    const int color_mask = args[4].to_uint16();
    const int color = adjust_graph_color(args[5].to_sint16());
    const int priority = machine.kernel_argument(6).to_sint16();
    const int control = machine.kernel_argument(7).to_sint16();
    gfx(machine).paint().fill_rect(graph_rect(args), static_cast<std::int16_t>(color_mask),
                                   static_cast<std::uint8_t>(color), static_cast<std::uint8_t>(priority),
                                   static_cast<std::uint8_t>(control));
    return machine.acc();
}

Reg k_graph_update_box(Machine& machine, std::span<Reg> args) {
    if (args.size() <= 5 || args[5].is_null()) {
        gfx(machine).paint().bits_show(graph_rect(args));
    }
    return machine.acc();
}

Reg k_graph_redraw_box(Machine& machine, std::span<Reg> args) {
    gfx(machine).paint().kernel_graph_redraw_box(graph_rect(args));
    return machine.acc();
}

Reg k_graph_adjust_priority(Machine& machine, std::span<Reg> args) {
    gfx(machine).ports().kernel_graph_adjust_priority(args[0].to_uint16(), args[1].to_uint16());
    return machine.acc();
}

Reg k_text_size(Machine& machine, std::span<Reg> args) {
    SegManager& segments = machine.segments();
    const std::span<Reg> dest = segments.deref_regs(args[0], 4);
    const std::string text = segments.get_string(args[1]);
    const int font = args[2].to_uint16();
    const int max_width = args.size() > 3 ? args[3].to_uint16() : 0;
    if (dest.empty()) {
        return machine.acc();
    }
    const char* separator = nullptr;
    std::string separator_text;
    if (args.size() > 4 && args[4].segment != 0) {
        separator_text = segments.get_string(args[4]);
        separator = separator_text.c_str();
    }
    dest[0] = null_reg;
    dest[1] = null_reg;
    if (text.empty()) {
        dest[2] = null_reg;
        dest[3] = null_reg;
        return machine.acc();
    }
    const std::string split = gfx(machine).str_split(text, separator);
    int width = 0;
    int height = 0;
    gfx(machine).text().kernel_text_size(split, font, max_width, width, height);
    dest[2] = number(height);
    dest[3] = number(width);
    return machine.acc();
}

Reg k_wait(Machine& machine, std::span<Reg> args) {
    const std::uint16_t delta = machine.subsystems().clock().wait(args[0].to_uint16());
    machine.subsystems().reset_event_counter();
    return number(delta);
}

Reg k_coord_pri(Machine& machine, std::span<Reg> args) {
    const int y = args[0].to_sint16();
    Ports& ports = gfx(machine).ports();
    if (args.size() < 2 || y != 1) {
        return number(ports.kernel_coordinate_to_priority(y));
    }
    return number(ports.kernel_priority_to_coordinate(static_cast<std::uint8_t>(args[1].to_sint16())));
}

Reg k_dir_loop(Machine& machine, std::span<Reg> args) {
    dir_loop_worker(machine, args[0], args[1].to_uint16());
    return machine.acc();
}

Reg k_can_be_here(Machine& machine, std::span<Reg> args) {
    const Reg list = args.size() > 1 ? args[1] : null_reg;
    const Reg result = gfx(machine).compare().kernel_can_be_here(args[0], list);
    return number(result.is_null() ? 1 : 0);
}

Reg k_cel_high(Machine& machine, std::span<Reg> args) {
    const int view = args[0].to_sint16();
    if (view == -1) {
        return null_reg;
    }
    const int cel = args.size() >= 3 ? args[2].to_sint16() : 0;
    return number(gfx(machine).cache().view(view).height(args[1].to_sint16(), cel));
}

Reg k_cel_wide(Machine& machine, std::span<Reg> args) {
    const int view = args[0].to_sint16();
    if (view == -1) {
        return null_reg;
    }
    const int cel = args.size() >= 3 ? args[2].to_sint16() : 0;
    return number(gfx(machine).cache().view(view).width(args[1].to_sint16(), cel));
}

Reg k_num_loops(Machine& machine, std::span<Reg> args) {
    const int view = machine.read_selector_value(args[0], machine.selector().view);
    return number(gfx(machine).cache().view(view).loop_count());
}

Reg k_num_cels(Machine& machine, std::span<Reg> args) {
    const int view = machine.read_selector_value(args[0], machine.selector().view);
    const int loop = machine.read_selector_value(args[0], machine.selector().loop);
    return number(gfx(machine).cache().view(view).cel_count(loop));
}

Reg k_on_control(Machine& machine, std::span<Reg> args) {
    std::uint8_t mask = mask_control;
    std::size_t base = 0;
    if (args.size() != 2 && args.size() != 4) {
        mask = static_cast<std::uint8_t>(args[0].to_uint16());
        base = 1;
    }
    Rect rect;
    rect.left = args[base].to_sint16();
    rect.top = args[base + 1].to_sint16();
    if (args.size() > 3) {
        rect.right = args[base + 2].to_sint16();
        rect.bottom = args[base + 3].to_sint16();
    } else {
        rect.right = static_cast<std::int16_t>(rect.left + 1);
        rect.bottom = static_cast<std::int16_t>(rect.top + 1);
    }
    return number(gfx(machine).compare().kernel_on_control(mask, rect));
}

Reg k_draw_pic(Machine& machine, std::span<Reg> args) {
    const int picture = args[0].to_uint16();
    int animation = -1;
    bool blackout = false;
    bool mirrored = false;
    bool add_to = false;
    int ega_palette = 0;
    if (args.size() >= 2) {
        const std::uint16_t flags = args[1].to_uint16();
        blackout = (flags & (1U << 15U)) != 0;
        animation = flags & 0xFF;
        mirrored = (flags & (1U << 14U)) != 0;
    }
    if (args.size() >= 3) {
        // Not the old graphics functions: the flag is inverted.
        add_to = args[2].is_null();
    }
    if (args.size() >= 4) {
        ega_palette = args[3].to_uint16();
    }
    gfx(machine).paint().kernel_draw_picture(picture, animation, blackout, mirrored, add_to, ega_palette);
    return machine.acc();
}

Reg k_base_setter(Machine& machine, std::span<Reg> args) {
    gfx(machine).compare().kernel_base_setter(args[0]);
    return machine.acc();
}

Reg k_set_now_seen(Machine& machine, std::span<Reg> args) {
    gfx(machine).compare().kernel_set_now_seen(args[0]);
    return machine.acc();
}

Reg k_add_to_pic(Machine& machine, std::span<Reg> args) {
    Animate& animate = gfx(machine).animate();
    switch (args.size()) {
    case 1:
        if (args[0].is_null()) {
            return machine.acc();
        }
        animate.kernel_add_to_pic_list(args[0]);
        break;
    case 7:
        animate.kernel_add_to_pic_view(args[0].to_uint16(), args[1].to_sint16(), args[2].to_sint16(),
                                       args[3].to_sint16(), args[4].to_sint16(), args[5].to_sint16(),
                                       args[6].to_sint16());
        break;
    default:
        machine.faults().raise(std::format("kAddToPic with unsupported parameter count {}", args.size()));
        break;
    }
    return machine.acc();
}

Reg k_get_port(Machine& machine, std::span<Reg> /*args*/) {
    return gfx(machine).ports().kernel_get_active();
}

Reg k_set_port(Machine& machine, std::span<Reg> args) {
    Ports& ports = gfx(machine).ports();
    switch (args.size()) {
    case 1:
        ports.kernel_set_active(static_cast<std::uint16_t>(args[0].to_sint16()));
        break;
    case 6:
    case 7: {
        const Rect rect{.top = args[0].to_sint16(),
                        .left = args[1].to_sint16(),
                        .bottom = args[2].to_sint16(),
                        .right = args[3].to_sint16()};
        ports.kernel_set_pic_window(rect, args[4].to_sint16(), args[5].to_sint16(), args.size() == 7);
        break;
    }
    default:
        machine.faults().raise(std::format("SetPort was called with {} parameters", args.size()));
        break;
    }
    return machine.acc();
}

Reg k_draw_cel(Machine& machine, std::span<Reg> args) {
    const int priority = args.size() > 5 ? args[5].to_sint16() : -1;
    const int palette = args.size() > 6 ? args[6].to_uint16() : 0;
    gfx(machine).paint().draw_cel_and_show(args[0].to_sint16(), args[1].to_sint16(), args[2].to_sint16(),
                                           args[3].to_uint16(), args[4].to_uint16(), priority, palette);
    return machine.acc();
}

Reg k_dispose_window(Machine& machine, std::span<Reg> args) {
    const bool reanimate = args.size() != 2 || args[1].is_null();
    gfx(machine).ports().kernel_dispose_window(static_cast<std::uint16_t>(args[0].to_sint16()), reanimate);
    return machine.acc();
}

Reg k_new_window(Machine& machine, std::span<Reg> args) {
    const Rect dims = Rect::from(args[1].to_sint16(), args[0].to_sint16(), args[3].to_sint16(), args[2].to_sint16());
    const std::size_t extra = args.size() >= 13 ? 4 : 0;
    Rect restore;
    if (args.size() >= 13) {
        restore = Rect::from(args[5].to_sint16(), args[4].to_sint16(), args[7].to_sint16(), args[6].to_sint16());
    }
    const int style = args[5 + extra].to_sint16();
    const int priority = args.size() > 6 + extra ? args[6 + extra].to_sint16() : -1;
    const int pen = adjust_graph_color(args.size() > 7 + extra ? args[7 + extra].to_sint16() : 0);
    const int back = adjust_graph_color(args.size() > 8 + extra ? args[8 + extra].to_sint16() : 255);
    std::string title;
    if (args[4 + extra].segment != 0) {
        title = gfx(machine).str_split(machine.segments().get_string(args[4 + extra]), nullptr);
    }
    return gfx(machine).ports().kernel_new_window(dims, restore, static_cast<std::uint16_t>(style), priority, pen, back,
                                                  title);
}

Reg k_animate(Machine& machine, std::span<Reg> args) {
    const Reg cast = arg_or(args, 0, null_reg);
    const bool cycle = args.size() > 1 && args[1].to_uint16() != 0;
    gfx(machine).animate().kernel_animate(cast, cycle, args);
    return machine.acc();
}

Reg k_shake_screen(Machine& machine, std::span<Reg> args) {
    int count = !args.empty() ? args[0].to_uint16() : 1;
    // The screen moves; time passes: three ticks each way.
    Clock& clock = machine.subsystems().clock();
    while (count-- > 0) {
        clock.sleep(3 * 1000 / 60);
        clock.sleep(3 * 1000 / 60);
    }
    return machine.acc();
}

Reg k_display(Machine& machine, std::span<Reg> args) {
    const Reg text_pointer = args[0];
    const int index = args.size() > 1 ? args[1].to_uint16() : 0;
    std::string text;
    std::span<Reg> rest;
    if (text_pointer.segment != 0) {
        rest = args.subspan(1);
        text = machine.segments().get_string(text_pointer);
    } else {
        rest = args.size() >= 2 ? args.subspan(2) : std::span<Reg>{};
        text = lookup_text(machine, text_pointer, index);
    }
    const std::string split = gfx(machine).str_split(text, "\r----------\r");
    return gfx(machine).paint().kernel_display(split, rest);
}

Reg k_show(Machine& machine, std::span<Reg> args) {
    const int map = args[0].to_uint16();
    if (map < 1 || map > 4) {
        warn(std::format("Map {} is not available", map));
    }
    return machine.acc();
}

Reg k_add_menu(Machine& machine, std::span<Reg> args) {
    SegManager& segments = machine.segments();
    gfx(machine).menu().kernel_add_entry(segments.get_string(args[0]), segments.get_string(args[1]), args[1]);
    return machine.acc();
}

Reg k_set_menu(Machine& machine, std::span<Reg> args) {
    const auto menu_id = static_cast<std::uint16_t>(args[0].to_uint16() >> 8U);
    const auto item_id = static_cast<std::uint16_t>(args[0].to_uint16() & 0xFFU);
    for (std::size_t i = 1; i < args.size(); i += 2) {
        const Reg value = i + 1 < args.size() ? args[i + 1] : null_reg;
        gfx(machine).menu().kernel_set_attribute(menu_id, item_id, args[i].to_uint16(), value);
    }
    return machine.acc();
}

Reg k_get_menu(Machine& machine, std::span<Reg> args) {
    const auto menu_id = static_cast<std::uint16_t>(args[0].to_uint16() >> 8U);
    const auto item_id = static_cast<std::uint16_t>(args[0].to_uint16() & 0xFFU);
    return gfx(machine).menu().kernel_get_attribute(menu_id, item_id, args[1].to_uint16());
}

Reg k_draw_status(Machine& machine, std::span<Reg> args) {
    const Reg text_pointer = args[0];
    const int pen = args.size() > 1 ? args[1].to_sint16() : 0;
    const int back = args.size() > 2 ? args[2].to_sint16() : Screen::color_white;
    if (!text_pointer.is_null()) {
        const std::string text = machine.segments().get_string(text_pointer);
        if (text == "Replaying sound") {
            return machine.acc();
        }
        gfx(machine).menu().kernel_draw_status(gfx(machine).str_split(text, nullptr), pen, back);
    }
    return machine.acc();
}

Reg k_draw_menu_bar(Machine& machine, std::span<Reg> args) {
    gfx(machine).menu().kernel_draw_menu_bar(args[0].is_null());
    return machine.acc();
}

} // namespace sci::vm
