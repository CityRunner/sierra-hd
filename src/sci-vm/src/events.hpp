// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/event.h, engines/sci/event.cpp (EventManager::getSciEvent)
// and translated into this project's style, types and error handling.
// Input arrives as SCI events with a game tick: from a replay file, or from
// the app, which converts host input (spec section 7).
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

#include "sci/vm/events.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <list>

namespace sci::vm {

class StateWriter;
class StateReader;

class EventQueue {
public:
    // Input due at a game tick, in order.
    void schedule(const InputEvent& event) { pending_.push_back(event); }

    // Input from the host now; key events take the mouse position.
    void push(SciEvent event) {
        if (event.type == event_key_down || event.type == event_key_up) {
            event.mouse = mouse_;
        } else {
            mouse_ = event.mouse;
        }
        queue_.push_back(event);
    }

    [[nodiscard]] bool has_pending() const { return !pending_.empty(); }

    // The input not yet due, in order.
    [[nodiscard]] const std::deque<InputEvent>& pending() const { return pending_; }

    // EventManager::getSciEvent: moves the input due at `tick` into the
    // queue and returns the first event the mask accepts (removing it
    // unless the mask peeks), or an empty event at the mouse position.
    SciEvent get(std::uint16_t mask, std::uint32_t tick);
    void flush(std::uint32_t tick);

    // The host asks to quit (Engine::quitGame): a quit event comes with the
    // next poll, before the replayed input.
    void request_quit() { quit_requested_ = true; }

    [[nodiscard]] bool quit_requested() const { return quit_requested_; }

    // Called at the start of every read (EventManager::getSciEvent calls
    // updateScreen there).
    void set_update_screen(std::function<void()> hook) { update_screen_ = std::move(hook); }

    [[nodiscard]] Point mouse() const { return mouse_; }

    void set_mouse(Point position) { mouse_ = position; }

    // Writes its part of the VM state (state.cpp).
    void save_state(StateWriter& out) const;
    // Reads it back (state_load.cpp).
    void load_state(StateReader& in);

private:
    void take_due(std::uint32_t tick);

    std::deque<InputEvent> pending_;
    std::list<SciEvent> queue_;
    Point mouse_{.x = 160, .y = 100};
    bool quit_requested_ = false;
    std::function<void()> update_screen_;
};

} // namespace sci::vm
