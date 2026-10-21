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

#include "graphics.hpp"
#include "machine.hpp"

#include <cctype>
#include <cstdlib>
#include <format>

namespace sci::vm {

namespace {

constexpr char replace_on_control = 0x03;
constexpr char replace_on_alt = 0x02;
constexpr char replace_on_function = 'F';
constexpr std::uint16_t key_f1 = 59 << 8;
constexpr std::uint16_t key_f10 = 68 << 8;

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
        .id = static_cast<std::uint16_t>(list_.size() + 1), .text = title, .text_split = {}, .text_width = 0});
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
                item.key_press = static_cast<std::uint16_t>(key_f1 + ((key - '1') << 8));
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
            item.tag = static_cast<std::uint16_t>(std::atoi(content.c_str() + tag_start));
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
    MenuItemEntry* item = find_item(menu_id, item_id);
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

} // namespace sci::vm
