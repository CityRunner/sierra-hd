// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/cache.cpp, view.cpp (SCI0 EGA views), scifont.cpp
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
#include "sci/text/font.hpp"
#include "sci/view/view.hpp"

#include <cstdint>
#include <map>

namespace sci::vm {

class Machine;
class Screen;

// A decoded view with GfxView's accessors: loop and cel numbers out of
// range are clamped.
class GfxView {
public:
    explicit GfxView(view::View view) : view_(std::move(view)) {}

    [[nodiscard]] int loop_count() const { return static_cast<int>(view_.loops.size()); }

    [[nodiscard]] int cel_count(int loop) const;
    [[nodiscard]] const view::Cel& cel(int loop, int cel) const;

    [[nodiscard]] int width(int loop, int cel) const { return view_.loops.empty() ? 0 : this->cel(loop, cel).width; }

    [[nodiscard]] int height(int loop, int cel) const { return view_.loops.empty() ? 0 : this->cel(loop, cel).height; }

    [[nodiscard]] Rect cel_rect(int loop, int cel, int x, int y, int z) const;

    // GfxView::draw without scaling or EGA mapping: `rect` is where the cel
    // is, `clip` the part to draw, `clip_translated` that part on screen.
    void draw(Screen& screen, const Rect& rect, const Rect& clip, const Rect& clip_translated, int loop, int cel,
              std::uint8_t priority) const;

private:
    [[nodiscard]] int clamp_loop(int loop) const;

    view::View view_;
};

class GfxFont {
public:
    GfxFont(int id, text::Font font) : id_(id), font_(std::move(font)) {}

    [[nodiscard]] int id() const { return id_; }

    [[nodiscard]] int height() const { return font_.line_height; }

    [[nodiscard]] int char_width(std::uint16_t chr) const {
        return chr < font_.glyphs.size() ? font_.glyphs[chr].width : 0;
    }

    void draw(Screen& screen, std::uint16_t chr, int top, int left, std::uint8_t color, bool greyed) const;

private:
    int id_;
    text::Font font_;
};

class GfxCache {
public:
    explicit GfxCache(Machine& machine) : machine_(&machine) {}

    // The view or font; a fault if the game has none (an empty one is
    // returned then).
    [[nodiscard]] const GfxView& view(int id);
    [[nodiscard]] const GfxFont& font(int id);

private:
    Machine* machine_;
    std::map<int, GfxView> views_;
    std::map<int, GfxFont> fonts_;
};

} // namespace sci::vm
