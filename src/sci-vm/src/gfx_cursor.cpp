// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/cursor.cpp, coordadjuster.cpp, transitions.cpp
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

#include "gfx_cursor.hpp"

#include "events.hpp"
#include "graphics.hpp"
#include "machine.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <algorithm>
#include <array>
#include <format>

namespace sci::vm {

namespace {

constexpr std::size_t cursor_resource_size = 68; // SCI_CURSOR_SCI0_RESOURCESIZE

constexpr int transition_fade_palette = 10;
constexpr int transition_none = 100;

struct TransitionEntry {
    int original = 0;
    int id = 0;
    bool blackout = false;
};

constexpr std::array old_transition_ids{
    TransitionEntry{.original = 0, .id = 0, .blackout = false},
    TransitionEntry{.original = 1, .id = 1, .blackout = false},
    TransitionEntry{.original = 2, .id = 2, .blackout = false},
    TransitionEntry{.original = 3, .id = 3, .blackout = false},
    TransitionEntry{.original = 4, .id = 4, .blackout = false},
    TransitionEntry{.original = 5, .id = 5, .blackout = false},
    TransitionEntry{.original = 6, .id = 6, .blackout = false},
    TransitionEntry{.original = 7, .id = 7, .blackout = false},
    TransitionEntry{.original = 8, .id = 8, .blackout = false},
    TransitionEntry{.original = 9, .id = 300, .blackout = false},
    TransitionEntry{.original = 10, .id = 301, .blackout = false},
    TransitionEntry{.original = 11, .id = 2, .blackout = true},
    TransitionEntry{.original = 12, .id = 3, .blackout = true},
    TransitionEntry{.original = 13, .id = 4, .blackout = true},
    TransitionEntry{.original = 14, .id = 5, .blackout = true},
    TransitionEntry{.original = 15, .id = 6, .blackout = true},
    TransitionEntry{.original = 16, .id = 7, .blackout = true},
    TransitionEntry{.original = 17, .id = 8, .blackout = true},
    TransitionEntry{.original = 18, .id = 9, .blackout = false},
    TransitionEntry{.original = 27, .id = 9, .blackout = true},
    TransitionEntry{.original = 30, .id = 10, .blackout = false},
    TransitionEntry{.original = 40, .id = 11, .blackout = false},
    TransitionEntry{.original = 41, .id = 12, .blackout = false},
    TransitionEntry{.original = 42, .id = 13, .blackout = false},
    TransitionEntry{.original = 43, .id = 14, .blackout = false},
    TransitionEntry{.original = 100, .id = 100, .blackout = false},
};

constexpr std::array blackout_transition_ids{
    TransitionEntry{.original = 0, .id = 300, .blackout = true},
    TransitionEntry{.original = 1, .id = 301, .blackout = true},
    TransitionEntry{.original = 2, .id = 3, .blackout = true},
    TransitionEntry{.original = 3, .id = 2, .blackout = true},
    TransitionEntry{.original = 4, .id = 5, .blackout = true},
    TransitionEntry{.original = 5, .id = 4, .blackout = true},
    TransitionEntry{.original = 7, .id = 6, .blackout = true},
    TransitionEntry{.original = 6, .id = 7, .blackout = true},
    TransitionEntry{.original = 8, .id = 8, .blackout = true},
    TransitionEntry{.original = 9, .id = 9, .blackout = true},
    TransitionEntry{.original = 10, .id = 100, .blackout = true},
    TransitionEntry{.original = 11, .id = 100, .blackout = true},
    TransitionEntry{.original = 12, .id = 100, .blackout = true},
    TransitionEntry{.original = 13, .id = 100, .blackout = true},
    TransitionEntry{.original = 14, .id = 100, .blackout = true},
    TransitionEntry{.original = 15, .id = 100, .blackout = true},
    TransitionEntry{.original = 100, .id = 100, .blackout = true},
    TransitionEntry{.original = 300, .id = 100, .blackout = true},
    TransitionEntry{.original = 301, .id = 100, .blackout = true},
};

template <std::size_t N>
const TransitionEntry* translate(int number, const std::array<TransitionEntry, N>& table) {
    const auto found = std::ranges::find(table, number, &TransitionEntry::original);
    return found == table.end() ? nullptr : &*found;
}

} // namespace

void Cursor::kernel_set_shape(int resource) {
    if (resource == -1) {
        kernel_hide();
        return;
    }
    const auto* data = graphics_->machine().resources().find(ResourceType::Cursor, resource);
    if (data == nullptr) {
        graphics_->faults().raise(std::format("cursor resource {} not found", resource));
        return;
    }
    if (data->size() != cursor_resource_size) {
        graphics_->faults().raise(std::format("cursor resource {} has invalid size", resource));
        return;
    }
    shape_ = resource;
    kernel_show();
}

void Cursor::kernel_set_pos(GPoint position) {
    // GfxCoordAdjuster16::setCursorPos: relative to the current port.
    const Port* port = graphics_->ports().port();
    position.y = static_cast<std::int16_t>(position.y + port->top);
    position.x = static_cast<std::int16_t>(position.x + port->left);
    kernel_move_cursor(position);
}

void Cursor::kernel_move_cursor(GPoint position) {
    // GfxCoordAdjuster16::moveCursor: within the picture window.
    const Rect& pic = graphics_->ports().pic_window()->rect;
    position.y = static_cast<std::int16_t>(position.y + pic.top);
    position.x = static_cast<std::int16_t>(position.x + pic.left);
    position.y = static_cast<std::int16_t>(std::clamp<int>(position.y, pic.top, pic.bottom - 1));
    position.x = static_cast<std::int16_t>(std::clamp<int>(position.x, pic.left, pic.right - 1));
    if (position.x > Screen::width || position.y > Screen::height) {
        warn(std::format("attempt to place cursor at invalid coordinates ({}, {})", position.y, position.x));
        return;
    }
    set_position(position);
    // Read events, so that the mouse is there for the next read.
    Machine& machine = graphics_->machine();
    machine.subsystems().events().get(event_peek, machine.subsystems().clock().ticks());
}

void Cursor::set_position(GPoint position) {
    // Not while hidden (EcoQuest 1 sets it all the time during the intro).
    if (!visible_) {
        return;
    }
    graphics_->machine().subsystems().events().set_mouse(Point{.x = position.x, .y = position.y});
}

GPoint Cursor::position() const {
    const Point mouse = graphics_->machine().subsystems().events().mouse();
    return GPoint{.x = mouse.x, .y = mouse.y};
}

void Transitions::setup(int number, bool blackout) {
    if (number != -1) {
        number_ = number;
        blackout_ = blackout;
    }
}

void Transitions::doit(const Rect& /*pic_rect*/) {
    if (const TransitionEntry* entry = translate(number_, old_transition_ids)) {
        number_ = entry->id;
        blackout_ = entry->blackout;
    } else {
        warn(std::format("Transitions: old ID {} not supported", number_));
        number_ = transition_none;
        blackout_ = false;
    }
    if (blackout_) {
        if (const TransitionEntry* entry = translate(number_, blackout_transition_ids)) {
            do_transition(entry->id, true);
        } else {
            warn(std::format("Transitions: ID {} not listed in blackoutTransitionIDs", number_));
        }
    }
    do_transition(number_, false);
    graphics_->screen().set_pic_not_valid(0);
}

void Transitions::do_transition(int number, bool blackout) {
    last_ = number;
    // Only the palette fade takes virtual time: 11 steps out and 11 in, two
    // ticks each (the others wait in real time, which the virtual clock
    // does not see).
    if (number == transition_fade_palette && !blackout) {
        Clock& clock = graphics_->machine().subsystems().clock();
        for (int step = 0; step < 22; ++step) {
            clock.sleep(2 * 1000 / 60);
        }
    }
}

} // namespace sci::vm
