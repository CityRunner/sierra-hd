// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/compare.{h,cpp}, coordadjuster.cpp (SCI0)
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

namespace sci::vm {

class Graphics;

class Compare {
public:
    explicit Compare(Graphics& graphics) : graphics_(&graphics) {}

    [[nodiscard]] std::uint16_t is_on_control(std::uint8_t screen_mask, const Rect& rect) const;
    std::uint16_t kernel_on_control(std::uint8_t screen_mask, const Rect& rect);
    void kernel_set_now_seen(Reg object);
    Reg kernel_can_be_here(Reg object, Reg list);
    void kernel_base_setter(Reg object);
    [[nodiscard]] Rect ns_rect(Reg object);
    void set_ns_rect(Reg object, const Rect& rect);
    // GfxCoordAdjuster16::onControl: the rectangle clipped to the picture
    // window, on screen.
    Rect on_control_rect(const Rect& rect);

private:
    Reg can_be_here_check_rect_list(Reg check_object, const Rect& check_rect, Reg list, std::uint16_t signal_flags);

    Graphics* graphics_;
};

} // namespace sci::vm
