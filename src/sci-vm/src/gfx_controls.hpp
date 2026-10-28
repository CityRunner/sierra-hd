// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/controls16.{h,cpp} and engine/kgraphics.cpp
//   (_k_GenericDrawControl, kDrawControl, kHiliteControl, kEditControl)
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
#include <string>
#include <vector>

namespace sci::vm {

class StateWriter;
class Graphics;

inline constexpr unsigned control_style_enabled = 0x0001U;
inline constexpr unsigned control_style_disabled = 0x0004U;
inline constexpr unsigned control_style_selected = 0x0008U;

inline constexpr int control_type_button = 1;
inline constexpr int control_type_text = 2;
inline constexpr int control_type_textedit = 3;
inline constexpr int control_type_icon = 4;
inline constexpr int control_type_list = 6;
inline constexpr int control_type_list_alias = 7;
inline constexpr int control_type_dummy = 10;

// GfxControls16: the dialog controls of SCI0 (buttons, text, edit fields,
// icons and lists) and the edit field's cursor.
class Controls {
public:
    explicit Controls(Graphics& graphics) : graphics_(&graphics) {}

    // _k_GenericDrawControl: draws `control`, or inverts it to show it
    // selected. The accumulator is set as ScummVM sets it.
    void draw_control(Reg control, bool hilite);
    // kEditControl on an edit field: applies a key event to its text.
    void kernel_textedit_change(Reg control, Reg event);

    // Writes its part of the VM state (state.cpp).
    void save_state(StateWriter& out) const;

private:
    void draw_button(Rect rect, const std::string& text, int font_id, unsigned style, bool hilite);
    void draw_text(Rect rect, const std::string& text, int font_id, int alignment, unsigned style, bool hilite);
    void draw_textedit(Rect rect, const std::string& text, int font_id, unsigned style, int cursor_position,
                       bool hilite);
    void draw_icon(Rect rect, int view, int loop, int cel, int priority, unsigned style, bool hilite);
    void draw_list(Rect rect, int max_chars, const std::vector<std::string>& entries, int font_id, unsigned style,
                   int upper_position, int cursor_position, bool is_alias, bool hilite);
    void draw_list_control(Rect rect, int max_chars, const std::vector<std::string>& entries, int font_id,
                           int upper_position, int cursor_position, bool is_alias);
    void textedit_cursor_draw(const Rect& rect, const std::string& text, std::size_t position);
    void textedit_cursor_erase();
    void textedit_set_blink_time();
    // The selected look of a button, text or icon.
    void invert(const Rect& rect);
    [[nodiscard]] bool pic_not_valid() const;

    Graphics* graphics_;
    Rect cursor_rect_;
    bool cursor_visible_ = false;
    std::uint32_t blink_time_ = 0;
};

} // namespace sci::vm
