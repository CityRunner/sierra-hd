// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/menu.cpp
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

#include "gfx_menu.hpp"

#include "events.hpp"
#include "graphics.hpp"
#include "machine.hpp"
#include "music.hpp"
#include "parser.hpp"
#include "sound.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

namespace sci::vm {

namespace {

constexpr char replace_on_control = 0x03;
constexpr char replace_on_alt = 0x02;
constexpr char replace_on_function = 'F';
constexpr std::uint16_t key_f1 = 59U << 8U;
constexpr std::uint16_t key_f10 = 68U << 8U;
constexpr std::uint16_t key_tab = '\t';
constexpr std::uint16_t modifier_non_sticky = 0x0FU; // Both shifts, Ctrl and Alt.

std::uint16_t lower(char c) {
    return static_cast<std::uint16_t>(std::tolower(static_cast<unsigned char>(c)));
}

char upper(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

} // namespace

void Menu::reset() {
    list_.clear();
    items_.clear();
    current_menu_ = 1;
    current_item_ = 1;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxMenu::kernelAddEntry.
void Menu::kernel_add_entry(const std::string& title, std::string content, Reg content_pointer) {
    Faults& faults = graphics_->faults();
    const int content_size = static_cast<int>(content.size());
    const auto at = [&](int position) {
        return position < content_size ? content[static_cast<std::size_t>(position)] : '\0';
    };
    list_.push_back(MenuEntry{
        .id = static_cast<std::uint16_t>(list_.size() + 1),
        .text = title,
        .text_split = {},
        .text_width = 0,
    });
    const auto menu_id = static_cast<std::uint16_t>(list_.size());
    std::uint16_t item_count = 0;
    int position = 0;
    do {
        ++item_count;
        MenuItemEntry item;
        item.menu_id = menu_id;
        item.id = item_count;
        const int begin = position;
        int tag_position = 0;
        int right_aligned_position = 0;
        int control_position = 0;
        int alt_position = 0;
        int function_position = 0;
        // ':' ends each item.
        while (position < content_size && at(position) != ':') {
            switch (at(position)) {
            case '=': // A tag; but a right-aligned "=" is the normal speed.
                if (right_aligned_position == position - 1) {
                    break;
                }
                if (tag_position != 0) {
                    faults.raise("multiple tag markers within one menu-item");
                    return;
                }
                tag_position = position;
                break;
            case '`':
                if (right_aligned_position != 0) {
                    warn("multiple right-aligned markers within one menu-item");
                }
                right_aligned_position = position;
                break;
            case '^':
                if (control_position != 0) {
                    faults.raise("multiple control markers within one menu-item");
                    return;
                }
                control_position = position;
                break;
            case '@':
                if (alt_position != 0) {
                    faults.raise("multiple alt markers within one menu-item");
                    return;
                }
                alt_position = position;
                break;
            case '#': // A function key once ` was seen (#G separates languages).
                if (right_aligned_position != 0) {
                    if (function_position != 0) {
                        faults.raise("multiple function markers within one menu-item");
                        return;
                    }
                    function_position = position;
                }
                break;
            default:
                break;
            }
            ++position;
        }
        const int end = position;
        const auto set = [&](int index, char c) { content[static_cast<std::size_t>(index)] = c; };
        if (control_position != 0) {
            set(control_position, replace_on_control);
            item.key_modifier = 1U << 2U; // kSciKeyModCtrl
            if (control_position + 1 >= content_size) {
                faults.raise("control marker at end of item");
                return;
            }
            item.key_press = lower(at(control_position + 1));
            set(control_position + 1, upper(at(control_position + 1)));
        }
        if (alt_position != 0) {
            set(alt_position, replace_on_alt);
            item.key_modifier = 1U << 3U; // kSciKeyModAlt
            if (alt_position + 1 >= content_size) {
                faults.raise("alt marker at end of item");
                return;
            }
            item.key_press = lower(at(alt_position + 1));
            set(alt_position + 1, upper(at(alt_position + 1)));
        }
        if (function_position != 0) {
            set(function_position, replace_on_function);
            if (function_position + 1 >= content_size) {
                faults.raise("function marker at end of item");
                return;
            }
            const char key = at(function_position + 1);
            if (key >= '1' && key <= '9') {
                item.key_press = static_cast<std::uint16_t>(key_f1 + (static_cast<unsigned>(key - '1') << 8U));
            } else if (key == '0') {
                item.key_press = key_f10;
            } else {
                faults.raise("illegal function key specified");
                return;
            }
        }
        // The strings.
        int text_end = end;
        if (right_aligned_position != 0) {
            text_end = right_aligned_position;
        } else if (tag_position != 0) {
            text_end = tag_position;
        }
        int separators = 0;
        for (int i = begin; i < text_end; ++i) {
            switch (at(i)) {
            case '!':
            case '-':
            case ' ':
                ++separators;
                break;
            case '%':
            case '#':
                separators += 2;
                ++i;
                break;
            default:
                break;
            }
        }
        if (separators == text_end - begin) {
            item.separator_line = true;
        } else {
            item.text = content.substr(static_cast<std::size_t>(begin), static_cast<std::size_t>(text_end - begin));
            if (const auto ctrl = item.text.find("Ctrl-"); ctrl != std::string::npos) {
                item.key_modifier = 1U << 2U;
                item.key_press = lower(ctrl + 5 < item.text.size() ? item.text[ctrl + 5] : '\0');
            }
        }
        item.text_pointer = content_pointer;
        item.text_pointer.offset = static_cast<std::uint16_t>(item.text_pointer.offset + begin);
        if (right_aligned_position != 0) {
            ++right_aligned_position;
            int right_end = end;
            if (tag_position != 0 && tag_position >= right_aligned_position) {
                right_end = tag_position;
            }
            item.text_right_aligned = content.substr(static_cast<std::size_t>(right_aligned_position),
                                                     static_cast<std::size_t>(right_end - right_aligned_position));
            if (item.text_right_aligned.ends_with(' ')) {
                item.text_right_aligned.pop_back();
            }
            if (item.text_right_aligned == "-" || item.text_right_aligned == "+" || item.text_right_aligned == "=") {
                item.key_press = static_cast<std::uint8_t>(item.text_right_aligned[0]);
            }
            if (item.key_press == key_f10) {
                if (const auto f10 = item.text_right_aligned.find("F0"); f10 != std::string::npos) {
                    item.text_right_aligned.insert(f10 + 1, 1, '1');
                }
            }
        }
        if (tag_position != 0) {
            // ScummVM reads the tag after the function marker, not the tag
            // marker; kept as it is.
            const int tag_start = function_position + 1;
            if (tag_start >= content_size) {
                faults.raise("tag marker at end of item");
                return;
            }
            // atoi, which glibc implements as strtol in base 10.
            const std::string tag_text = content.substr(static_cast<std::size_t>(tag_start));
            item.tag = static_cast<std::uint16_t>(static_cast<int>(std::strtol(tag_text.c_str(), nullptr, 10)));
        }
        position = end + 1;
        items_.push_back(std::move(item));
    } while (position < content_size);
}

MenuItemEntry* Menu::find_item(std::uint16_t menu_id, std::uint16_t item_id) {
    for (MenuItemEntry& item : items_) {
        if (item.menu_id == menu_id && item.id == item_id) {
            return &item;
        }
    }
    return nullptr;
}

void Menu::kernel_set_attribute(std::uint16_t menu_id, std::uint16_t item_id, std::uint16_t attribute, Reg value) {
    MenuItemEntry* item = find_item(menu_id, item_id);
    if (item == nullptr) {
        return; // Ignored, as the original interpreter did.
    }
    switch (attribute) {
    case menu_enabled:
        item->enabled = !value.is_null();
        break;
    case menu_said:
        item->said = value;
        break;
    case menu_text:
        item->text = graphics_->machine().segments().get_string(value);
        item->text_pointer = value;
        break;
    case menu_keypress:
        item->key_press = static_cast<std::uint16_t>(std::tolower(value.offset));
        item->key_modifier = 0;
        break;
    case menu_tag:
        item->tag = value.offset;
        break;
    default:
        warn(std::format("setAttribute() called with unsupported attributeId {:X}", attribute));
        break;
    }
}

Reg Menu::kernel_get_attribute(std::uint16_t menu_id, std::uint16_t item_id, std::uint16_t attribute) {
    const MenuItemEntry* item = find_item(menu_id, item_id);
    if (item == nullptr) {
        graphics_->faults().raise(
            std::format("Tried to getAttribute() on non-existent menu-item {}:{}", menu_id, item_id));
        return null_reg;
    }
    switch (attribute) {
    case menu_enabled:
        return item->enabled ? number(1) : null_reg;
    case menu_said:
        return item->said;
    case menu_text:
        return item->text_pointer;
    case menu_keypress:
        return number(item->key_press);
    case menu_tag:
        return number(item->tag);
    default:
        graphics_->faults().raise(std::format("getAttribute() called with unsupported attributeId {:X}", attribute));
        return null_reg;
    }
}

void Menu::draw_bar() {
    Ports& ports = graphics_->ports();
    Paint& paint = graphics_->paint();
    paint.fill_rect(ports.menu_bar_rect(), mask_visual, Screen::color_white, 0, 0);
    paint.fill_rect(ports.menu_line(), mask_visual, 0, 0, 0);
    ports.pen_color(0);
    ports.move_to(8, 1);
    for (const MenuEntry& entry : list_) {
        graphics_->text().draw_string(entry.text_split);
    }
}

void Menu::calculate_menu_width() {
    for (MenuEntry& entry : list_) {
        entry.text_split = graphics_->str_split(entry.text, nullptr);
        int height = 0;
        graphics_->text().string_width(entry.text_split, entry.text_width, height);
    }
}

void Menu::kernel_draw_status(const std::string& text, int pen_color, int back_color) {
    Ports& ports = graphics_->ports();
    Paint& paint = graphics_->paint();
    Port* old_port = ports.set_port(ports.menu_port());
    paint.fill_rect(ports.menu_bar_rect(), mask_visual, static_cast<std::uint8_t>(back_color), 0, 0);
    ports.pen_color(pen_color);
    ports.move_to(0, 1);
    graphics_->text().draw_status(text);
    paint.bits_show(ports.menu_bar_rect());
    // The line under the status bar too (Dr. Brain 1 Mac draws over it).
    paint.fill_rect(ports.menu_line(), mask_visual, 0, 0, 0);
    paint.bits_show(ports.menu_line());
    ports.set_port(old_port);
}

void Menu::kernel_draw_menu_bar(bool clear) {
    if (clear) {
        kernel_draw_status("", 0, 0);
        return;
    }
    Ports& ports = graphics_->ports();
    Port* old_port = ports.set_port(ports.menu_port());
    calculate_menu_width();
    draw_bar();
    graphics_->paint().bits_show(ports.menu_bar_rect());
    ports.set_port(old_port);
}

void Menu::calculate_menu_and_item_width() {
    calculate_menu_width();
    for (MenuItemEntry& item : items_) {
        // Split now: multilingual SCI01 games switch languages at any time.
        item.text_split = graphics_->str_split(item.text, nullptr);
        int height = 0;
        graphics_->text().string_width(item.text_split, item.text_width, height);
        graphics_->text().string_width(item.text_right_aligned, item.text_right_aligned_width, height);
    }
}

bool Menu::interrupted() {
    Machine& machine = graphics_->machine();
    return machine.faults().raised() || machine.aborting() != Abort::None;
}

MenuItemEntry* Menu::match_key(std::uint16_t key_press, std::uint16_t key_modifier) {
    for (MenuItemEntry& item : items_) {
        // Tab and Ctrl+I share the ASCII character, but the modifier is
        // checked too, so Tab becomes Ctrl+I.
        if (key_press == key_tab) {
            key_modifier = modifier_ctrl;
            key_press = 'i';
        }
        // Only the low byte of the modifiers (the keyboard driver bug of
        // kGetEvent).
        key_modifier &= 0xFFU;
        if (item.key_press == key_press && item.key_modifier == key_modifier && item.enabled) {
            return &item;
        }
    }
    return nullptr;
}

MenuItemEntry* Menu::match_said() {
    Machine& machine = graphics_->machine();
    for (MenuItemEntry& item : items_) {
        if (!item.said.is_null() && item.enabled && machine.subsystems().parser().said(machine, item.said)) {
            return &item;
        }
    }
    return nullptr;
}

Reg Menu::kernel_select(Reg event_object, bool pause_sound) {
    Machine& machine = graphics_->machine();
    const SelectorCache& s = machine.selector();
    Paint& paint = graphics_->paint();
    Ports& ports = graphics_->ports();
    const auto event_type = static_cast<std::uint16_t>(machine.read_selector_value(event_object, s.type));
    const MenuItemEntry* item = nullptr;
    bool force_claimed = false;
    if (event_type == event_key_down) {
        auto key_press = static_cast<std::uint16_t>(machine.read_selector_value(event_object, s.message));
        const auto key_modifier = static_cast<std::uint16_t>(machine.read_selector_value(event_object, s.modifiers));
        // Ctrl+<key> puts an ASCII control character in the message; the
        // items match the printable character with the modifier.
        if ((key_modifier & modifier_non_sticky) == modifier_ctrl && key_press > 0 && key_press < 27) {
            key_press = static_cast<std::uint16_t>(key_press + 96);
        }
        if (key_press == key_escape) {
            interactive_start(pause_sound);
            item = interactive_with_keyboard();
            interactive_end(pause_sound);
            force_claimed = true;
        } else if (key_press != 0) {
            item = match_key(key_press, key_modifier);
        }
    } else if (event_type == event_said) {
        item = match_said();
    } else if (event_type == event_mouse_press) {
        const int y = machine.read_selector_value(event_object, s.y);
        if (y < 10) {
            interactive_start(pause_sound);
            item = interactive_with_mouse();
            interactive_end(pause_sound);
            force_claimed = true;
        }
    }
    if (!menu_save_handle_.is_null()) {
        remove_menu();
        menu_save_handle_ = null_reg;
    }
    if (!bar_save_handle_.is_null()) {
        paint.bits_restore(bar_save_handle_);
        paint.bits_show(ports.menu_rect());
        bar_save_handle_ = null_reg;
    }
    if (old_port_ != nullptr) {
        ports.set_port(old_port_);
        old_port_ = nullptr;
    }
    if (item != nullptr || force_claimed) {
        machine.write_selector(event_object, s.claimed, number(1));
    }
    if (item != nullptr) {
        return number(static_cast<int>((static_cast<unsigned>(item->menu_id) << 8U) | item->id));
    }
    return null_reg;
}

MenuItemEntry* Menu::interactive_get_item(std::uint16_t menu_id, std::uint16_t item_id, bool menu_changed) {
    MenuItemEntry* first = nullptr;
    MenuItemEntry* last = nullptr;
    if (menu_id > list_.size()) {
        menu_id = 1;
    }
    if (menu_id == 0) {
        menu_id = static_cast<std::uint16_t>(list_.size());
    }
    for (MenuItemEntry& item : items_) {
        if (item.menu_id == menu_id) {
            if (item.id == item_id) {
                return &item;
            }
            if (first == nullptr) {
                first = &item;
            }
            if (last == nullptr || item.id > last->id) {
                last = &item;
            }
        }
    }
    if (item_id == 0 || menu_changed) {
        return last;
    }
    return first;
}

// Removes the open menu: its background, then the line between the bar and
// the menu, then the cast over the box.
void Menu::remove_menu() {
    Paint& paint = graphics_->paint();
    paint.bits_restore(menu_save_handle_);
    Rect line = menu_rect_;
    line.bottom = static_cast<std::int16_t>(line.top + 1);
    paint.bits_show(line);
    paint.kernel_graph_redraw_box(menu_rect_);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxMenu::drawMenu.
void Menu::draw_menu(std::uint16_t old_menu_id, std::uint16_t new_menu_id) {
    Ports& ports = graphics_->ports();
    Paint& paint = graphics_->paint();
    Screen& screen = graphics_->screen();
    Text& text = graphics_->text();
    if (!menu_save_handle_.is_null()) {
        remove_menu();
    }
    // The menu's rectangle; the old and the new title inverted.
    menu_rect_.top = ports.menu_bar_rect().bottom;
    Rect title{.top = ports.menu_bar_rect().top, .left = 7, .bottom = ports.menu_bar_rect().bottom, .right = 7};
    std::uint16_t number_in_list = 0;
    for (const MenuEntry& entry : list_) {
        ++number_in_list;
        title.left = title.right;
        title.right = static_cast<std::int16_t>(title.right + entry.text_width);
        if (number_in_list == new_menu_id) {
            menu_rect_.left = title.left;
        }
        if (number_in_list == new_menu_id || number_in_list == old_menu_id) {
            title.translate(1, 0);
            paint.invert_rect(title);
            title.translate(-1, 0);
        }
    }
    paint.bits_show(ports.menu_bar_rect());

    const int font_height = ports.port()->font_height;
    menu_rect_.bottom = static_cast<std::int16_t>(menu_rect_.top + 2);
    int max_text_width = 0;
    int max_right_width = 0;
    for (const MenuItemEntry& item : items_) {
        if (item.menu_id == new_menu_id) {
            menu_rect_.bottom = static_cast<std::int16_t>(menu_rect_.bottom + font_height);
            max_text_width = std::max(max_text_width, item.text_width);
            max_right_width = std::max(max_right_width, item.text_right_aligned_width);
        }
    }
    menu_rect_.right = static_cast<std::int16_t>(menu_rect_.left + 16 + 4 + 2 + max_text_width + max_right_width);
    if (max_right_width == 0) {
        menu_rect_.right = static_cast<std::int16_t>(menu_rect_.right - 5);
    }
    // A menu past the right edge moves into the screen.
    if (menu_rect_.right > Screen::width) {
        menu_rect_.translate(-(menu_rect_.right - Screen::width), 0);
    }
    if (menu_rect_.left < 0) {
        warn("GfxMenu::drawMenu: _menuRect.left < 0");
    }
    menu_save_handle_ = paint.bits_save(menu_rect_, mask_visual);

    paint.fill_rect(menu_rect_, mask_visual, 0, 0, 0);
    ++menu_rect_.left;
    --menu_rect_.right;
    --menu_rect_.bottom;
    paint.fill_rect(menu_rect_, mask_visual, Screen::color_white, 0, 0);
    menu_rect_.left = static_cast<std::int16_t>(menu_rect_.left + 8);
    int top = menu_rect_.top + 1;
    for (const MenuItemEntry& item : items_) {
        if (item.menu_id != new_menu_id) {
            continue;
        }
        if (!item.separator_line) {
            ports.text_greyed_output(!item.enabled);
            ports.move_to(menu_rect_.left, top);
            text.draw_string(item.text_split);
            ports.move_to(menu_rect_.right - item.text_right_aligned_width - 5, top);
            text.draw_string(item.text_right_aligned);
        } else {
            // A dotted line from left to right (ScummVM's choice).
            const int y = top + (font_height / 2) - 1;
            for (int x = menu_rect_.left - 7; x < menu_rect_.right - 1; x += 2) {
                screen.put_pixel(x, y, mask_visual, 0, 0, 0);
            }
        }
        top += font_height;
    }
    ports.text_greyed_output(false);
    // The black line again.
    paint.fill_rect(ports.menu_line(), mask_visual, 0, 0, 0);
    menu_rect_.left = static_cast<std::int16_t>(menu_rect_.left - 9);
    ++menu_rect_.right;
    ++menu_rect_.bottom;
    paint.bits_show(menu_rect_);
}

void Menu::invert_menu_selection(std::uint16_t item_id) {
    if (item_id == 0) {
        return;
    }
    const int font_height = graphics_->ports().port()->font_height;
    Rect item = menu_rect_;
    item.top = static_cast<std::int16_t>(item.top + ((item_id - 1) * font_height) + 1);
    item.bottom = static_cast<std::int16_t>(item.top + font_height);
    ++item.left;
    --item.right;
    graphics_->paint().invert_rect(item);
    graphics_->paint().bits_show(item);
}

void Menu::interactive_start(bool pause_sound) {
    Cursor& cursor = graphics_->cursor();
    mouse_old_state_ = cursor.visible();
    cursor.kernel_show();
    if (pause_sound) {
        graphics_->machine().subsystems().sound().music().pause_all(true);
    }
}

void Menu::interactive_end(bool pause_sound) {
    if (pause_sound) {
        graphics_->machine().subsystems().sound().music().pause_all(false);
    }
    if (!mouse_old_state_) {
        graphics_->cursor().kernel_hide();
    }
}

std::uint16_t Menu::mouse_find_menu_selection(GPoint position) const {
    int x = 8;
    for (const MenuEntry& entry : list_) {
        if (position.x >= x && position.x < x + entry.text_width) {
            return entry.id;
        }
        x += entry.text_width;
    }
    return 0;
}

std::uint16_t Menu::mouse_find_menu_item_selection(GPoint position, std::uint16_t menu_id) const {
    if (menu_id == 0 || position.x < menu_rect_.left || position.x >= menu_rect_.right) {
        return 0;
    }
    const int font_height = graphics_->ports().port()->font_height;
    int y = 10;
    std::uint16_t item_id = 0;
    for (const MenuItemEntry& item : items_) {
        if (item.menu_id == menu_id) {
            y += font_height;
            if (item_id == 0 && y > position.y) {
                item_id = item.id;
            }
        }
    }
    return item_id;
}

// Opens the bar and the current menu for the keyboard.
void Menu::open_for_keyboard(std::uint16_t menu_id, std::uint16_t item_id) {
    Ports& ports = graphics_->ports();
    Paint& paint = graphics_->paint();
    old_port_ = ports.set_port(ports.menu_port());
    calculate_menu_and_item_width();
    bar_save_handle_ = paint.bits_save(ports.menu_rect(), mask_visual);
    ports.pen_color(0);
    ports.back_color(Screen::color_white);
    draw_bar();
    draw_menu(0, menu_id);
    invert_menu_selection(item_id);
    paint.bits_show(ports.menu_rect());
    paint.bits_show(menu_rect_);
}

// The arrow keys move the selection; past a separator line the selection
// moves on down. Sets `current`; returns true when Escape or Enter (on an
// enabled item) ends the menu, with `chosen` set for Enter.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxMenu::interactiveWithKeyboard.
bool Menu::keyboard_step(SciEvent event, MenuItemEntry*& current, std::uint16_t& new_menu_id,
                         std::uint16_t& new_item_id, MenuItemEntry*& chosen) {
    MenuItemEntry* next = current;
    // ScummVM wraps around and never selects item 0, unlike Sierra.
    do {
        switch (event.character) {
        case key_escape:
            current_menu_ = current->menu_id;
            current_item_ = current->id;
            chosen = nullptr;
            return true;
        case key_enter:
            if (current->enabled) {
                current_menu_ = current->menu_id;
                current_item_ = current->id;
                chosen = current;
                return true;
            }
            break;
        case key_left:
            --new_menu_id;
            new_item_id = 1;
            break;
        case key_right:
            ++new_menu_id;
            new_item_id = 1;
            break;
        case key_up:
            --new_item_id;
            break;
        case key_down:
            ++new_item_id;
            break;
        default:
            break;
        }
        if (new_menu_id != current->menu_id || new_item_id != current->id) {
            next = interactive_get_item(new_menu_id, new_item_id, new_menu_id != current->menu_id);
            new_menu_id = next->menu_id;
            new_item_id = next->id;
            // Past a separator line: go down, not left or right again.
            if (event.character == key_left || event.character == key_right) {
                event.character = key_down;
            }
        }
    } while (next->separator_line);
    if (new_menu_id != current->menu_id || new_item_id != current->id) {
        if (new_menu_id != current->menu_id) {
            draw_menu(current->menu_id, new_menu_id);
        } else {
            invert_menu_selection(current->id);
        }
        invert_menu_selection(new_item_id);
        current = next;
    }
    return false;
}

// A click while the menu is open from the keyboard: on the bar it opens
// that menu, below it chooses an item.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxMenu::interactiveWithKeyboard.
bool Menu::keyboard_click(GPoint position, MenuItemEntry*& current, std::uint16_t& new_menu_id,
                          std::uint16_t& new_item_id, MenuItemEntry*& chosen) {
    if (position.y < 10) {
        new_menu_id = mouse_find_menu_selection(position);
        if (new_menu_id != 0) {
            new_item_id = 1;
            MenuItemEntry* next = interactive_get_item(new_menu_id, new_item_id, new_menu_id != current->menu_id);
            if (new_menu_id != current->menu_id) {
                draw_menu(current->menu_id, new_menu_id);
            } else {
                invert_menu_selection(current->id);
            }
            invert_menu_selection(new_item_id);
            current = next;
        } else {
            new_menu_id = current->menu_id;
        }
        return false;
    }
    new_item_id = mouse_find_menu_item_selection(position, new_menu_id);
    if (new_item_id != 0) {
        MenuItemEntry* next = interactive_get_item(new_menu_id, new_item_id, false);
        if (next->enabled && !next->separator_line) {
            current_menu_ = next->menu_id;
            current_item_ = next->id;
            chosen = next;
            return true;
        }
    }
    new_item_id = current->id;
    return false;
}

MenuItemEntry* Menu::interactive_with_keyboard() {
    Subsystems& subsystems = graphics_->machine().subsystems();
    std::uint16_t new_menu_id = current_menu_;
    std::uint16_t new_item_id = current_item_;
    MenuItemEntry* current = find_item(current_menu_, current_item_);
    if (current == nullptr) {
        graphics_->faults().raise("GfxMenu::interactiveWithKeyboard: no current menu item");
        return nullptr;
    }
    // ScummVM keeps the last selection where Sierra started at the first item.
    open_for_keyboard(current->menu_id, current->id);
    MenuItemEntry* chosen = nullptr;
    while (!interrupted()) {
        const SciEvent event = subsystems.events().get(event_any, subsystems.clock().ticks());
        bool done = false;
        switch (event.type) {
        case event_key_down:
            done = keyboard_step(event, current, new_menu_id, new_item_id, chosen);
            break;
        case event_mouse_press:
            done = keyboard_click(GPoint{.x = event.mouse.x, .y = event.mouse.y}, current, new_menu_id, new_item_id,
                                  chosen);
            break;
        case event_none:
            subsystems.clock().sleep(2500 / 1000);
            break;
        case event_quit:
            return nullptr;
        default:
            break;
        }
        if (done) {
            return chosen;
        }
    }
    return nullptr;
}

// The button is down: follow the mouse until it is released, and choose
// the item under it then. No keyboard here.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxMenu::interactiveWithMouse.
MenuItemEntry* Menu::interactive_with_mouse() {
    Subsystems& subsystems = graphics_->machine().subsystems();
    Ports& ports = graphics_->ports();
    Paint& paint = graphics_->paint();
    std::uint16_t new_menu_id = 0;
    std::uint16_t new_item_id = 0;
    std::uint16_t current_menu = 0;
    std::uint16_t current_item = 0;
    bool first_menu_change = true;
    MenuItemEntry* current = nullptr;
    old_port_ = ports.set_port(ports.menu_port());
    calculate_menu_and_item_width();
    bar_save_handle_ = paint.bits_save(ports.menu_rect(), mask_visual);
    ports.pen_color(0);
    ports.back_color(Screen::color_white);
    draw_bar();
    paint.bits_show(ports.menu_rect());

    // ScummVM's loop does not end on a quit event; it waits for the release.
    while (!interrupted()) {
        const SciEvent event = subsystems.events().get(event_any, subsystems.clock().ticks());
        if (event.type == event_mouse_release) {
            if (current_menu == 0 || current_item == 0 || current == nullptr || !current->enabled ||
                current->separator_line) {
                return nullptr;
            }
            return current;
        }
        if (event.type == event_none) {
            subsystems.clock().sleep(2500 / 1000);
        }
        const GPoint position{.x = event.mouse.x, .y = event.mouse.y};
        if (position.y < 10) {
            new_menu_id = mouse_find_menu_selection(position);
            new_item_id = 0;
        } else {
            new_item_id = mouse_find_menu_item_selection(position, new_menu_id);
            current = interactive_get_item(current_menu, new_item_id, false);
        }
        if (new_item_id != current_item) {
            invert_menu_selection(current_item);
            invert_menu_selection(new_item_id);
            current_item = new_item_id;
        }
        if (new_menu_id != current_menu) {
            draw_menu(current_menu, new_menu_id);
            if (first_menu_change) {
                paint.bits_show(ports.menu_bar_rect());
                first_menu_change = false;
            }
            current_menu = new_menu_id;
        }
    }
    return nullptr;
}

} // namespace sci::vm
