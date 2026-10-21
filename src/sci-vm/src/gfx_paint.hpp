// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/paint16.{h,cpp} (SCI0 late)
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
#include "sci/pic/picture_maps.hpp"
#include "sci/vm/reg.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sci::vm {

class Graphics;
class GfxView;

class Paint {
public:
    explicit Paint(Graphics& graphics) : graphics_(&graphics) {}

    void draw_picture(int picture, bool mirrored, bool add_to, int ega_palette);
    void draw_cel_and_show(int view, int loop, int cel, int left, int top, int priority, int palette);
    void draw_cel(const GfxView& view, int loop, int cel, const Rect& cel_rect, int priority);
    void clear_screen(std::uint8_t color);
    void erase_rect(const Rect& rect);
    void paint_rect(const Rect& rect);
    void fill_rect(const Rect& rect, int draw_flags, std::uint8_t color, std::uint8_t priority, std::uint8_t control);
    void frame_rect(const Rect& rect);
    void invert_rect(const Rect& rect);

    // Copies a rectangle of the port to what is shown (copyRectToScreen).
    void bits_show(const Rect& rect);
    Reg bits_save(const Rect& rect, std::uint8_t mask);
    [[nodiscard]] Rect bits_rect(Reg handle) const;
    void bits_restore(Reg handle);
    void bits_free(Reg handle);

    void kernel_draw_picture(int picture, int animation, bool blackout, bool mirrored, bool add_to, int ega_palette);
    void kernel_graph_draw_line(GPoint start, GPoint end, int color, int priority, int control);
    void kernel_graph_frame_box(const Rect& rect, int color);
    void kernel_graph_redraw_box(Rect rect);
    Reg kernel_display(const std::string& text, std::span<Reg> args);

    // The last picture drawn without adding to the previous one, with the
    // visual map as it left it, for the HD background; and the one shown,
    // which a transition (or a redraw under a window) makes the drawn one.
    struct Picture {
        int number = -1;
        bool mirrored = false;
        int ega_palette = 0;
        std::vector<std::uint8_t> visual;
    };

    void picture_shown() { shown_picture_ = drawn_picture_; }

    [[nodiscard]] const Picture& shown_picture() const { return shown_picture_; }

private:
    Graphics* graphics_;
    pic::PictureMaps maps_; // The colour pairs carry over between pictures added to each other.
    Picture drawn_picture_;
    Picture shown_picture_;
};

} // namespace sci::vm
