// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/animate.{h,cpp} (SCI0: no scaling, no fast cast)
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
#include "sci/vm/reg.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::vm {

class Graphics;

enum ViewSignal : std::uint16_t {
    signal_stop_update = 0x0001,
    signal_view_updated = 0x0002,
    signal_no_update = 0x0004,
    signal_hidden = 0x0008,
    signal_fixed_priority = 0x0010,
    signal_always_update = 0x0020,
    signal_force_update = 0x0040,
    signal_remove_view = 0x0080,
    signal_frozen = 0x0100,
    signal_hit_obstacle = 0x0400,
    signal_doesnt_turn = 0x0800,
    signal_ignore_actor = 0x4000,
    signal_dispose_me = 0x8000,
};

struct AnimateEntry {
    int given_order = 0;
    Reg object;
    int view = 0;
    std::int16_t loop = 0;
    std::int16_t cel = 0;
    std::int16_t palette = 0;
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
    std::int16_t priority = 0;
    std::uint16_t signal = 0;
    Rect cel_rect;
    bool show_bits = false;
    Reg cast_handle;
};

class Animate {
public:
    explicit Animate(Graphics& graphics) : graphics_(&graphics) {}

    void kernel_animate(Reg list, bool cycle, std::span<Reg> args);
    void kernel_add_to_pic_list(Reg list);
    void kernel_add_to_pic_view(int view, int loop, int cel, int x, int y, int priority, int control);
    void re_animate(const Rect& rect);

    void dispose_last_cast() { last_cast_.clear(); }

private:
    bool invoke(Reg list);
    void make_sorted_list(Reg list);
    void fill(int& old_pic_not_valid);
    void update();
    void draw_cels();
    void update_screen(int old_pic_not_valid);
    void restore_and_delete();
    void add_to_pic_draw_cels();
    void show_pic();

    Graphics* graphics_;
    std::vector<AnimateEntry> list_;
    std::vector<AnimateEntry> last_cast_;
};

} // namespace sci::vm
