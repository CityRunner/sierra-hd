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

class StateWriter;
class Graphics;

inline constexpr unsigned signal_stop_update = 0x0001U;
inline constexpr unsigned signal_view_updated = 0x0002U;
inline constexpr unsigned signal_no_update = 0x0004U;
inline constexpr unsigned signal_hidden = 0x0008U;
inline constexpr unsigned signal_fixed_priority = 0x0010U;
inline constexpr unsigned signal_always_update = 0x0020U;
inline constexpr unsigned signal_force_update = 0x0040U;
inline constexpr unsigned signal_remove_view = 0x0080U;
inline constexpr unsigned signal_frozen = 0x0100U;
inline constexpr unsigned signal_hit_obstacle = 0x0400U;
inline constexpr unsigned signal_doesnt_turn = 0x0800U;
inline constexpr unsigned signal_ignore_actor = 0x4000U;
inline constexpr unsigned signal_dispose_me = 0x8000U;

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

    // The cast of the last kAnimate call in drawing order, with the values
    // it was drawn with; empty if that call drew nothing (for the actor
    // positions of the deterministic runs).
    [[nodiscard]] const std::vector<AnimateEntry>& drawn_cast() const { return drawn_cast_; }

    // Writes its part of the VM state (state.cpp).
    void save_state(StateWriter& out) const;

private:
    bool invoke(Reg list);
    void make_sorted_list(Reg list);
    void fill(int& old_pic_not_valid);
    void update();
    void draw_cels();
    void draw_sprite(const AnimateEntry& it);
    void update_screen(int old_pic_not_valid);
    void restore_and_delete();
    void add_to_pic_draw_cels();
    void show_pic();

    Graphics* graphics_;
    std::vector<AnimateEntry> list_;
    std::vector<AnimateEntry> last_cast_;
    std::vector<AnimateEntry> drawn_cast_;
};

} // namespace sci::vm
