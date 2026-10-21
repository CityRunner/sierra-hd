// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/animate.cpp (SCI0: no scaling, no fast cast)
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

#include "gfx_animate.hpp"

#include "graphics.hpp"
#include "machine.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <algorithm>
#include <ranges>

namespace sci::vm {

bool Animate::invoke(Reg list) {
    Machine& machine = graphics_->machine();
    const SegManager& segments = machine.segments();
    const SelectorCache& s = machine.selector();
    Reg address = segments.lookup_list(list)->first;
    const Node* node = segments.lookup_node(address);
    while (node != nullptr) {
        const Reg object = node->value;
        const auto signal = static_cast<std::uint16_t>(machine.read_selector_value(object, s.signal));
        if ((signal & signal_frozen) == 0) {
            machine.invoke_selector(object, s.doit, {});
            if (machine.aborting() != Abort::None || machine.faults().raised()) {
                return true;
            }
            // The node table may have moved, or the node be gone.
            node = segments.lookup_node(address, false);
        }
        if (node != nullptr) {
            address = node->succ;
            node = segments.lookup_node(address);
        }
    }
    return true;
}

void Animate::make_sorted_list(Reg list) {
    Machine& machine = graphics_->machine();
    const SegManager& segments = machine.segments();
    const SelectorCache& s = machine.selector();
    list_.clear();
    last_cast_.clear();
    Reg address = segments.lookup_list(list)->first;
    const Node* node = segments.lookup_node(address);
    for (int number = 0; node != nullptr; ++number) {
        const Reg object = node->value;
        AnimateEntry entry;
        entry.object = object;
        entry.cast_handle = null_reg;
        entry.given_order = number;
        entry.view = machine.read_selector_value(object, s.view);
        entry.loop = machine.read_selector_value(object, s.loop);
        entry.cel = machine.read_selector_value(object, s.cel);
        entry.palette = machine.read_selector_value(object, s.palette);
        entry.x = machine.read_selector_value(object, s.x);
        entry.y = machine.read_selector_value(object, s.y);
        entry.z = machine.read_selector_value(object, s.z);
        entry.priority = machine.read_selector_value(object, s.priority);
        entry.signal = static_cast<std::uint16_t>(machine.read_selector_value(object, s.signal));
        entry.show_bits = false;
        list_.push_back(entry);
        address = node->succ;
        node = segments.lookup_node(address);
    }
    // By y, then z, then the given order: a total order, so the sort is the
    // one ScummVM's (unstable) sort gives.
    std::ranges::sort(list_, [](const AnimateEntry& a, const AnimateEntry& b) {
        if (a.y == b.y) {
            if (a.z == b.z) {
                return a.given_order < b.given_order;
            }
            return a.z < b.z;
        }
        return a.y < b.y;
    });
}

void Animate::fill(int& old_pic_not_valid) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    for (AnimateEntry& it : list_) {
        const GfxView& view = graphics_->cache().view(it.view);
        // Invalid loops and cels: Sierra compared signed for the reset to 0
        // and unsigned later; games rely on both.
        const int loop_count = view.loop_count();
        if (it.loop >= loop_count) {
            it.loop = 0;
            machine.write_selector_value(it.object, s.loop, it.loop);
        } else if (it.loop < 0) {
            it.loop = static_cast<std::int16_t>(loop_count - 1);
        }
        const int cel_count = view.cel_count(it.loop);
        if (it.cel >= cel_count) {
            it.cel = 0;
            machine.write_selector_value(it.object, s.cel, it.cel);
        } else if (it.cel < 0) {
            it.cel = static_cast<std::int16_t>(cel_count - 1);
        }
        it.cel_rect = view.cel_rect(it.loop, it.cel, it.x, it.y, it.z);
        graphics_->compare().set_ns_rect(it.object, it.cel_rect);

        if ((it.signal & signal_fixed_priority) == 0) {
            it.priority = graphics_->ports().kernel_coordinate_to_priority(it.y);
            machine.write_selector_value(it.object, s.priority, it.priority);
        }
        if ((it.signal & signal_no_update) != 0) {
            if ((it.signal & (signal_force_update | signal_view_updated)) != 0 ||
                ((it.signal & signal_hidden) != 0 && (it.signal & signal_remove_view) == 0) ||
                ((it.signal & signal_hidden) == 0 && (it.signal & signal_remove_view) != 0) ||
                (it.signal & signal_always_update) != 0) {
                ++old_pic_not_valid;
            }
            it.signal &= static_cast<std::uint16_t>(~signal_stop_update);
        } else {
            if ((it.signal & signal_stop_update) != 0 || (it.signal & signal_always_update) != 0) {
                ++old_pic_not_valid;
            }
            it.signal &= static_cast<std::uint16_t>(~signal_force_update);
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxAnimate::update.
void Animate::update() {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    Paint& paint = graphics_->paint();
    Ports& ports = graphics_->ports();
    const auto fill_control = [&](const AnimateEntry& it) {
        Rect rect = it.cel_rect;
        rect.top = static_cast<std::int16_t>(std::clamp(ports.kernel_priority_to_coordinate(it.priority) - 1,
                                                        static_cast<int>(rect.top), rect.bottom - 1));
        paint.fill_rect(rect, mask_control, 0, 0, 15);
    };

    // Remove the no-update cels.
    for (auto& it : std::views::reverse(list_)) {
        if ((it.signal & signal_no_update) != 0) {
            if ((it.signal & signal_remove_view) == 0) {
                const Reg bits = machine.read_selector(it.object, s.under_bits);
                if (graphics_->screen().pic_not_valid != 1) {
                    paint.bits_restore(bits);
                    it.show_bits = true;
                } else {
                    paint.bits_free(bits);
                }
                machine.write_selector_value(it.object, s.under_bits, 0);
            }
            it.signal &= static_cast<std::uint16_t>(~signal_force_update);
            if ((it.signal & signal_view_updated) != 0) {
                it.signal &= static_cast<std::uint16_t>(~(signal_view_updated | signal_no_update));
            }
        } else if ((it.signal & signal_stop_update) != 0) {
            it.signal &= static_cast<std::uint16_t>(~signal_stop_update);
            it.signal |= signal_no_update;
        }
    }
    // Draw the always-update cels.
    for (AnimateEntry& it : list_) {
        if ((it.signal & signal_always_update) != 0) {
            paint.draw_cel(graphics_->cache().view(it.view), it.loop, it.cel, it.cel_rect, it.priority);
            it.show_bits = true;
            it.signal &= static_cast<std::uint16_t>(
                ~(signal_stop_update | signal_view_updated | signal_no_update | signal_force_update));
            if ((it.signal & signal_ignore_actor) == 0) {
                fill_control(it);
            }
        }
    }
    // Save the background of the no-update cels.
    for (AnimateEntry& it : list_) {
        if ((it.signal & signal_no_update) != 0) {
            if ((it.signal & signal_hidden) != 0) {
                it.signal |= signal_remove_view;
            } else {
                it.signal &= static_cast<std::uint16_t>(~signal_remove_view);
                const std::uint8_t mask =
                    (it.signal & signal_ignore_actor) != 0 ? mask_visual | mask_priority : mask_all;
                machine.write_selector(it.object, s.under_bits, paint.bits_save(it.cel_rect, mask));
            }
        }
    }
    // Draw the no-update cels.
    for (AnimateEntry& it : list_) {
        if ((it.signal & signal_no_update) != 0 && (it.signal & signal_hidden) == 0) {
            paint.draw_cel(graphics_->cache().view(it.view), it.loop, it.cel, it.cel_rect, it.priority);
            it.show_bits = true;
            if ((it.signal & signal_ignore_actor) == 0) {
                fill_control(it);
            }
        }
    }
}

void Animate::draw_cels() {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    Paint& paint = graphics_->paint();
    last_cast_.clear();
    for (AnimateEntry& it : list_) {
        if ((it.signal & (signal_no_update | signal_hidden | signal_always_update)) == 0) {
            machine.write_selector(it.object, s.under_bits, paint.bits_save(it.cel_rect, mask_all));
            paint.draw_cel(graphics_->cache().view(it.view), it.loop, it.cel, it.cel_rect, it.priority);
            it.show_bits = true;
            if ((it.signal & signal_remove_view) != 0) {
                it.signal &= static_cast<std::uint16_t>(~signal_remove_view);
            }
            last_cast_.push_back(it);
        }
    }
}

void Animate::update_screen(int old_pic_not_valid) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    Paint& paint = graphics_->paint();
    for (AnimateEntry& it : list_) {
        if (it.show_bits || !((it.signal & (signal_remove_view | signal_no_update)) != 0 ||
                              ((it.signal & signal_remove_view) == 0 && (it.signal & signal_no_update) != 0 &&
                               old_pic_not_valid != 0))) {
            const Rect ls_rect{
                .top = machine.read_selector_value(it.object, s.ls_top),
                .left = machine.read_selector_value(it.object, s.ls_left),
                .bottom = machine.read_selector_value(it.object, s.ls_bottom),
                .right = machine.read_selector_value(it.object, s.ls_right),
            };
            Rect worker = ls_rect;
            worker.clip(it.cel_rect);
            if (!worker.is_empty()) {
                worker = ls_rect;
                worker.extend(it.cel_rect);
            } else {
                paint.bits_show(ls_rect);
                worker = it.cel_rect;
            }
            machine.write_selector_value(it.object, s.ls_left, it.cel_rect.left);
            machine.write_selector_value(it.object, s.ls_top, it.cel_rect.top);
            machine.write_selector_value(it.object, s.ls_right, it.cel_rect.right);
            machine.write_selector_value(it.object, s.ls_bottom, it.cel_rect.bottom);
            paint.bits_show(worker);
            if ((it.signal & signal_hidden) != 0) {
                it.signal |= signal_remove_view;
            }
        }
    }
}

void Animate::restore_and_delete() {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    // A separate loop: a dispose may change another object's signal.
    for (const AnimateEntry& it : list_) {
        machine.write_selector_value(it.object, s.signal, it.signal);
    }
    for (auto& it : std::views::reverse(list_)) {
        it.signal = static_cast<std::uint16_t>(machine.read_selector_value(it.object, s.signal));
        if ((it.signal & (signal_no_update | signal_remove_view)) == 0) {
            graphics_->paint().bits_restore(machine.read_selector(it.object, s.under_bits));
            machine.write_selector_value(it.object, s.under_bits, 0);
        }
        if ((it.signal & signal_dispose_me) != 0) {
            machine.invoke_selector(it.object, s.delete_, {});
        }
    }
}

void Animate::re_animate(const Rect& rect) {
    Paint& paint = graphics_->paint();
    if (last_cast_.empty()) {
        paint.bits_show(rect);
        return;
    }
    for (AnimateEntry& it : last_cast_) {
        it.cast_handle = paint.bits_save(it.cel_rect, mask_visual | mask_priority);
        paint.draw_cel(graphics_->cache().view(it.view), it.loop, it.cel, it.cel_rect, it.priority);
    }
    paint.bits_show(rect);
    for (auto& it : std::views::reverse(last_cast_)) {
        paint.bits_restore(it.cast_handle);
    }
}

void Animate::add_to_pic_draw_cels() {
    Paint& paint = graphics_->paint();
    const Ports& ports = graphics_->ports();
    for (AnimateEntry& it : list_) {
        const GfxView& view = graphics_->cache().view(it.view);
        // kAddToPic does not fix loop and cel numbers.
        if (it.priority == -1) {
            it.priority = ports.kernel_coordinate_to_priority(it.y);
        }
        it.cel_rect = view.cel_rect(it.loop, it.cel, it.x, it.y, it.z);
        paint.draw_cel(view, it.loop, it.cel, it.cel_rect, it.priority);
        if ((it.signal & signal_ignore_actor) == 0) {
            it.cel_rect.top =
                static_cast<std::int16_t>(std::clamp(ports.kernel_priority_to_coordinate(it.priority) - 1,
                                                     static_cast<int>(it.cel_rect.top), it.cel_rect.bottom - 1));
            paint.fill_rect(it.cel_rect, mask_control, 0, 0, 15);
        }
    }
}

void Animate::show_pic() {
    const Ports& ports = graphics_->ports();
    Cursor& cursor = graphics_->cursor();
    const bool cursor_was_visible = cursor.visible();
    Rect pic_rect = ports.pic_window()->rect;
    if (cursor_was_visible) {
        cursor.kernel_hide();
    }
    pic_rect.translate(ports.pic_window()->left, ports.pic_window()->top);
    graphics_->transitions().doit(pic_rect);
    if (cursor_was_visible) {
        cursor.kernel_show();
    }
}

void Animate::kernel_animate(Reg list, bool cycle, std::span<Reg> /*args*/) {
    Machine& machine = graphics_->machine();
    const Screen& screen = graphics_->screen();
    Ports& ports = graphics_->ports();
    int old_pic_not_valid = screen.pic_not_valid;
    if (list.is_null()) {
        dispose_last_cast();
        if (screen.pic_not_valid != 0) {
            show_pic();
        }
        return;
    }
    if (machine.segments().lookup_list(list) == nullptr) {
        machine.faults().raise("kAnimate called with non-list as parameter");
        return;
    }
    if (cycle) {
        if (!invoke(list)) {
            return;
        }
        // ScummVM goes on drawing after an abort; only a fault stops here.
        if (machine.faults().raised()) {
            return;
        }
        if (machine.segments().lookup_list(list) == nullptr) {
            machine.faults().raise("kAnimate: the cast list is gone");
            return;
        }
    }
    Port* old_port = ports.set_port(ports.pic_window());
    dispose_last_cast();
    make_sorted_list(list);
    fill(old_pic_not_valid);
    // old_pic_not_valid is a byte in ScummVM.
    old_pic_not_valid %= 256;
    if (old_pic_not_valid != 0) {
        update();
    }
    draw_cels();
    if (screen.pic_not_valid != 0) {
        show_pic();
    }
    update_screen(old_pic_not_valid);
    restore_and_delete();
    ports.set_port(old_port);
    machine.subsystems().clock().set_throttle_trigger(true);
}

void Animate::kernel_add_to_pic_list(Reg list) {
    Machine& machine = graphics_->machine();
    Ports& ports = graphics_->ports();
    ports.set_port(ports.pic_window());
    if (machine.segments().lookup_list(list) == nullptr) {
        machine.faults().raise("kAddToPic called with non-list as parameter");
        return;
    }
    make_sorted_list(list);
    add_to_pic_draw_cels();
    graphics_->screen().pic_not_valid = 1;
}

void Animate::kernel_add_to_pic_view(int view, int loop, int cel, int x, int y, int priority, int control) {
    Ports& ports = graphics_->ports();
    Paint& paint = graphics_->paint();
    ports.set_port(ports.pic_window());
    const GfxView& found = graphics_->cache().view(view);
    if (priority == -1) {
        priority = ports.kernel_coordinate_to_priority(y);
    }
    Rect cel_rect = found.cel_rect(loop, cel, x, y, 0);
    paint.draw_cel(found, loop, cel, cel_rect, priority);
    if (control != -1) {
        cel_rect.top = static_cast<std::int16_t>(std::clamp(ports.kernel_priority_to_coordinate(priority) - 1,
                                                            static_cast<int>(cel_rect.top), cel_rect.bottom - 1));
        paint.fill_rect(cel_rect, mask_control, 0, 0, static_cast<std::uint8_t>(control));
    }
    graphics_->screen().pic_not_valid = 1;
}

} // namespace sci::vm
