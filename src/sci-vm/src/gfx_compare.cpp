// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/compare.cpp, coordadjuster.cpp (SCI0)
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

#include "gfx_compare.hpp"

#include "graphics.hpp"
#include "machine.hpp"

#include <format>

namespace sci::vm {

std::uint16_t Compare::is_on_control(std::uint8_t screen_mask, const Rect& rect) const {
    if (rect.is_empty()) {
        return 0;
    }
    const Screen& screen = graphics_->screen();
    unsigned result = 0;
    const bool priority = (screen_mask & mask_priority) != 0;
    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
            result |= 1U << (priority ? screen.priority(x, y) : screen.control(x, y));
        }
    }
    return static_cast<std::uint16_t>(result);
}

Rect Compare::on_control_rect(const Rect& rect) {
    Ports& ports = graphics_->ports();
    Port* old_port = ports.set_port(ports.pic_window());
    Rect adjusted = rect;
    adjusted.clip(ports.port()->rect);
    ports.offset_rect(adjusted);
    ports.set_port(old_port);
    return adjusted;
}

std::uint16_t Compare::kernel_on_control(std::uint8_t screen_mask, const Rect& rect) {
    return is_on_control(screen_mask, on_control_rect(rect));
}

Reg Compare::can_be_here_check_rect_list(Reg check_object, const Rect& check_rect, Reg list,
                                         std::uint16_t signal_flags) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    const SegManager& segments = machine.segments();
    const List* found = segments.lookup_list(list);
    Reg address = found->first;
    const Node* node = segments.lookup_node(address);
    while (node != nullptr) {
        const Reg object = node->value;
        if (object != check_object) {
            const auto signal = static_cast<std::uint16_t>(machine.read_selector_value(object, s.signal));
            if ((signal & signal_flags) == 0) {
                const Rect rect{
                    .top = machine.read_selector_value(object, s.br_top),
                    .left = machine.read_selector_value(object, s.br_left),
                    .bottom = machine.read_selector_value(object, s.br_bottom),
                    .right = machine.read_selector_value(object, s.br_right),
                };
                // Not contains(): a rect is not within itself (KQ4 early).
                if (rect.right > check_rect.left && rect.left < check_rect.right && rect.bottom > check_rect.top &&
                    rect.top < check_rect.bottom) {
                    return object;
                }
            }
        }
        address = node->succ;
        node = segments.lookup_node(address);
    }
    return null_reg;
}

void Compare::kernel_set_now_seen(Reg object) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    const int view = machine.read_selector_value(object, s.view);
    const int loop = machine.read_selector_value(object, s.loop);
    const int cel = machine.read_selector_value(object, s.cel);
    const int x = machine.read_selector_value(object, s.x);
    const int y = machine.read_selector_value(object, s.y);
    const int z = s.z != no_selector ? machine.read_selector_value(object, s.z) : 0;
    const Rect cel_rect = graphics_->cache().view(view).cel_rect(loop, cel, x, y, z);
    if (machine.lookup_selector(object, s.ns_top, nullptr, nullptr, nullptr) == Machine::SelectorKind::Variable) {
        set_ns_rect(object, cel_rect);
    }
}

Reg Compare::kernel_can_be_here(Reg object, Reg list) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    const Rect check_rect{
        .top = machine.read_selector_value(object, s.br_top),
        .left = machine.read_selector_value(object, s.br_left),
        .bottom = machine.read_selector_value(object, s.br_bottom),
        .right = machine.read_selector_value(object, s.br_right),
    };
    const auto signal = static_cast<std::uint16_t>(machine.read_selector_value(object, s.signal));
    if (!check_rect.is_valid()) {
        warn(std::format("kCan(t)BeHere - invalid rect {}, {} -> {}, {}", check_rect.left, check_rect.top,
                         check_rect.right, check_rect.bottom));
        return null_reg; // Can be here.
    }
    const Rect adjusted = on_control_rect(check_rect);
    const auto control_mask = static_cast<std::uint16_t>(machine.read_selector_value(object, s.illegal_bits));
    const std::uint16_t result = is_on_control(mask_control, adjusted) & control_mask;
    if (result == 0 && (signal & (signal_ignore_actor | signal_remove_view)) == 0) {
        if (machine.segments().lookup_list(list) == nullptr) {
            machine.faults().raise("kCanBeHere called with non-list as parameter");
            return null_reg;
        }
        return can_be_here_check_rect_list(object, check_rect, list,
                                           signal_ignore_actor | signal_remove_view | signal_no_update);
    }
    return make_reg(0, result);
}

void Compare::kernel_base_setter(Reg object) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    if (machine.lookup_selector(object, s.br_left, nullptr, nullptr, nullptr) != Machine::SelectorKind::Variable) {
        return;
    }
    const int x = machine.read_selector_value(object, s.x);
    const int y = machine.read_selector_value(object, s.y);
    const int z = s.z != no_selector ? machine.read_selector_value(object, s.z) : 0;
    const int y_step = machine.read_selector_value(object, s.y_step);
    const int view = machine.read_selector_value(object, s.view);
    const int loop = machine.read_selector_value(object, s.loop);
    const int cel = machine.read_selector_value(object, s.cel);
    Rect cel_rect = graphics_->cache().view(view).cel_rect(loop, cel, x, y, z);
    cel_rect.bottom = static_cast<std::int16_t>(y + 1);
    cel_rect.top = static_cast<std::int16_t>(cel_rect.bottom - y_step);
    machine.write_selector_value(object, s.br_left, cel_rect.left);
    machine.write_selector_value(object, s.br_right, cel_rect.right);
    machine.write_selector_value(object, s.br_top, cel_rect.top);
    machine.write_selector_value(object, s.br_bottom, cel_rect.bottom);
}

Rect Compare::ns_rect(Reg object) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    return Rect{
        .top = machine.read_selector_value(object, s.ns_top),
        .left = machine.read_selector_value(object, s.ns_left),
        .bottom = machine.read_selector_value(object, s.ns_bottom),
        .right = machine.read_selector_value(object, s.ns_right),
    };
}

void Compare::set_ns_rect(Reg object, const Rect& rect) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    machine.write_selector_value(object, s.ns_left, rect.left);
    machine.write_selector_value(object, s.ns_top, rect.top);
    machine.write_selector_value(object, s.ns_right, rect.right);
    machine.write_selector_value(object, s.ns_bottom, rect.bottom);
}

} // namespace sci::vm
