// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/menu.{h,cpp} (entries, attributes and the bar;
//   kMenuSelect is milestone 4b)
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

#include "sci/vm/reg.hpp"

#include <cstdint>
#include <list>
#include <string>

namespace sci::vm {

class Graphics;

inline constexpr std::uint16_t menu_said = 0x6d;
inline constexpr std::uint16_t menu_text = 0x6e;
inline constexpr std::uint16_t menu_keypress = 0x6f;
inline constexpr std::uint16_t menu_enabled = 0x70;
inline constexpr std::uint16_t menu_tag = 0x71;

struct MenuEntry {
    std::uint16_t id = 0;
    std::string text;
    std::string text_split;
    int text_width = 0;
};

struct MenuItemEntry {
    std::uint16_t menu_id = 0;
    std::uint16_t id = 0;
    bool enabled = true;
    std::uint16_t tag = 0;
    std::uint16_t key_press = 0;
    std::uint16_t key_modifier = 0;
    bool separator_line = false;
    Reg said;
    std::string text;
    std::string text_split;
    Reg text_pointer;
    int text_width = 0;
    std::string text_right_aligned;
    int text_right_aligned_width = 0;
};

class Menu {
public:
    explicit Menu(Graphics& graphics) : graphics_(&graphics) {}

    void reset();
    void kernel_add_entry(const std::string& title, std::string content, Reg content_pointer);
    void kernel_set_attribute(std::uint16_t menu_id, std::uint16_t item_id, std::uint16_t attribute, Reg value);
    Reg kernel_get_attribute(std::uint16_t menu_id, std::uint16_t item_id, std::uint16_t attribute);
    void kernel_draw_status(const std::string& text, int pen_color, int back_color);
    void kernel_draw_menu_bar(bool clear);

    [[nodiscard]] const std::list<MenuEntry>& menus() const { return list_; }

    [[nodiscard]] const std::list<MenuItemEntry>& items() const { return items_; }

private:
    MenuItemEntry* find_item(std::uint16_t menu_id, std::uint16_t item_id);
    void draw_bar();
    void calculate_menu_width();

    Graphics* graphics_;
    std::list<MenuEntry> list_;
    std::list<MenuItemEntry> items_;
    std::uint16_t current_menu_ = 1;
    std::uint16_t current_item_ = 1;
};

} // namespace sci::vm
