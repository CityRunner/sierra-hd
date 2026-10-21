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

#include "gfx_rect.hpp"
#include "sci/vm/events.hpp"
#include "sci/vm/reg.hpp"

#include <cstdint>
#include <list>
#include <string>

namespace sci::vm {

class Graphics;
struct Port;

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
    // kMenuSelect: an event for the menu (Escape, a menu key, a click on
    // the bar, or a Said event); the item chosen as (menu << 8) | item.
    Reg kernel_select(Reg event_object, bool pause_sound);

    [[nodiscard]] const std::list<MenuEntry>& menus() const { return list_; }

    [[nodiscard]] const std::list<MenuItemEntry>& items() const { return items_; }

private:
    MenuItemEntry* find_item(std::uint16_t menu_id, std::uint16_t item_id);
    void draw_bar();
    void calculate_menu_width();
    void calculate_menu_and_item_width();
    MenuItemEntry* interactive_get_item(std::uint16_t menu_id, std::uint16_t item_id, bool menu_changed);
    void draw_menu(std::uint16_t old_menu_id, std::uint16_t new_menu_id);
    void remove_menu();
    void invert_menu_selection(std::uint16_t item_id);
    void interactive_start(bool pause_sound);
    void interactive_end(bool pause_sound);
    [[nodiscard]] std::uint16_t mouse_find_menu_selection(GPoint position) const;
    [[nodiscard]] std::uint16_t mouse_find_menu_item_selection(GPoint position, std::uint16_t menu_id) const;
    void open_for_keyboard(std::uint16_t menu_id, std::uint16_t item_id);
    bool keyboard_step(SciEvent event, MenuItemEntry*& current, std::uint16_t& new_menu_id, std::uint16_t& new_item_id,
                       MenuItemEntry*& chosen);
    bool keyboard_click(GPoint position, MenuItemEntry*& current, std::uint16_t& new_menu_id,
                        std::uint16_t& new_item_id, MenuItemEntry*& chosen);
    MenuItemEntry* interactive_with_keyboard();
    MenuItemEntry* interactive_with_mouse();
    MenuItemEntry* match_key(std::uint16_t key_press, std::uint16_t key_modifier);
    MenuItemEntry* match_said();
    // Whether the interactive loop must end: a fault, or the run stopping.
    [[nodiscard]] bool interrupted();

    Graphics* graphics_;
    std::list<MenuEntry> list_;
    std::list<MenuItemEntry> items_;
    std::uint16_t current_menu_ = 1;
    std::uint16_t current_item_ = 1;
    Rect menu_rect_; // The open menu (GfxMenu::_menuRect).
    Reg menu_save_handle_;
    Reg bar_save_handle_;
    Port* old_port_ = nullptr;
    bool mouse_old_state_ = false;
};

} // namespace sci::vm
