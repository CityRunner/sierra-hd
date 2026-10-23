// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/cursor.cpp, coordadjuster.cpp, transitions.cpp (state
//   and timing only: the frames are the renderer's)
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

#include <cstdint>

namespace sci::vm {

class Graphics;

class Cursor {
public:
    explicit Cursor(Graphics& graphics) : graphics_(&graphics) {}

    void kernel_show();
    void kernel_hide();

    [[nodiscard]] bool visible() const { return visible_; }

    [[nodiscard]] int shape() const { return shape_; }

    void kernel_set_shape(int resource);
    void kernel_set_pos(GPoint position);
    void kernel_move_cursor(GPoint position);
    [[nodiscard]] GPoint position() const;

private:
    void set_position(GPoint position);

    Graphics* graphics_;
    bool visible_ = false;
    int shape_ = -1;
};

class Transitions {
public:
    explicit Transitions(Graphics& graphics) : graphics_(&graphics) {}

    void setup(int number, bool blackout);
    void doit(const Rect& pic_rect);

    // The last transition shown, for the renderer.
    [[nodiscard]] int last() const { return last_; }

private:
    void do_transition(int number, bool blackout);

    Graphics* graphics_;
    int number_ = 1; // SCI_TRANSITIONS_HORIZONTALROLL_FROMCENTER
    bool blackout_ = false;
    int last_ = -1;
};

} // namespace sci::vm
