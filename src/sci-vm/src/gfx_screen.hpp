// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/screen.{h,cpp} (SCI0 at 320x200, no upscaling)
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

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sci::vm {

inline constexpr unsigned mask_visual = 1U;
inline constexpr unsigned mask_priority = 2U;
inline constexpr unsigned mask_control = 4U;
inline constexpr unsigned mask_all = mask_visual | mask_priority | mask_control;

// The visual, priority and control maps of the 320x200 screen. The display
// map ScummVM keeps beside the visual map holds the same colours here
// (dithering is not undone, the default of ScummVM).
class Screen {
public:
    static constexpr int width = 320;
    static constexpr int height = 200;
    static constexpr std::uint8_t color_white = 15;

    Screen();

    void put_pixel(int x, int y, std::uint8_t draw_mask, std::uint8_t color, std::uint8_t priority,
                   std::uint8_t control);

    [[nodiscard]] std::uint8_t visual(int x, int y) const { return visual_[index(x, y)]; }

    [[nodiscard]] std::uint8_t priority(int x, int y) const { return priority_[index(x, y)]; }

    [[nodiscard]] std::uint8_t control(int x, int y) const { return control_[index(x, y)]; }

    static std::uint8_t drawing_mask(std::uint8_t color, std::uint8_t priority, std::uint8_t control);
    void draw_line(GPoint start, GPoint end, std::uint8_t color, std::uint8_t priority, std::uint8_t control);

    // Saved bits: the rectangle, the mask, then the maps' pixels, laid out as
    // GfxScreen::bitsSave does (the visual map twice: visual and display).
    [[nodiscard]] static std::size_t bits_data_size(const Rect& rect, std::uint8_t mask);
    void bits_save(const Rect& rect, std::uint8_t mask, std::span<std::uint8_t> memory) const;
    [[nodiscard]] static Rect bits_rect(std::span<const std::uint8_t> memory);
    void bits_restore(std::span<const std::uint8_t> memory);

    // The display after a picture: the maps as the picture drew them.
    [[nodiscard]] std::vector<std::uint8_t>& visual_map() { return visual_; }

    [[nodiscard]] std::vector<std::uint8_t>& priority_map() { return priority_; }

    [[nodiscard]] std::vector<std::uint8_t>& control_map() { return control_; }

    [[nodiscard]] std::int16_t kernel_pic_not_valid(std::int16_t new_value);

    // 0, 1 or 2: whether the picture shown is out of date.
    [[nodiscard]] int pic_not_valid() const { return pic_not_valid_; }

    void set_pic_not_valid(int value) { pic_not_valid_ = value; }

private:
    [[nodiscard]] static std::size_t index(int x, int y) {
        return (static_cast<std::size_t>(y) * width) + static_cast<std::size_t>(x);
    }

    std::vector<std::uint8_t> visual_;
    std::vector<std::uint8_t> priority_;
    std::vector<std::uint8_t> control_;
    int pic_not_valid_ = 0;
};

} // namespace sci::vm
