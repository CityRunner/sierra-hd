// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/helpers.h (Port, Window), ports.{h,cpp} (SCI0 late)
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

#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sci::vm {

class Graphics;

inline constexpr std::uint16_t first_window_id = 2;
inline constexpr std::uint16_t first_script_window_id = 3;

enum WindowStyle : std::uint16_t {
    style_transparent = 1U << 0U,
    style_no_frame = 1U << 1U,
    style_title = 1U << 2U,
    style_topmost = 1U << 3U,
    style_user = 1U << 7U,
};

struct Port {
    explicit Port(std::uint16_t port_id) : id(port_id) {}

    Port(const Port&) = default;
    Port& operator=(const Port&) = default;
    Port(Port&&) = default;
    Port& operator=(Port&&) = default;
    virtual ~Port() = default;

    [[nodiscard]] bool is_window() const { return id >= first_window_id && id != 0xFFFF; }

    std::uint16_t id;
    std::int16_t top = 0;
    std::int16_t left = 0;
    Rect rect;
    std::int16_t cur_top = 0;
    std::int16_t cur_left = 0;
    std::int16_t font_height = 0;
    int font_id = 0;
    bool greyed_output = false;
    std::int16_t pen_color = 0;
    std::int16_t back_color = 0xFF;
    std::int16_t pen_mode = 0;
    std::uint16_t counter_till_free = 0;
};

struct Window : Port {
    explicit Window(std::uint16_t window_id) : Port(window_id) {}

    Rect dims;         // Client area.
    Rect restore_rect; // The whole window with its frame.
    std::uint16_t style = 0;
    std::uint16_t save_screen_mask = 0;
    Reg saved_visual;
    Reg saved_priority;
    std::string title;
    bool drawn = false;
};

class Ports {
public:
    explicit Ports(Graphics& graphics) : graphics_(&graphics) {}

    void init();
    void reset();

    void kernel_set_active(std::uint16_t port_id);
    void kernel_set_pic_window(const Rect& rect, std::int16_t pic_top, std::int16_t pic_left, bool init_bands);

    [[nodiscard]] Reg kernel_get_active() const { return make_reg(0, current_->id); }

    Reg kernel_new_window(const Rect& dims, const Rect& restore_rect, std::uint16_t style, int priority, int pen_color,
                          int back_color, const std::string& title);
    void kernel_dispose_window(std::uint16_t window_id, bool reanimate);

    [[nodiscard]] bool is_front_window(const Window* window) const { return windows_.back() == window; }

    void begin_update(Window* window);
    void end_update(Window* window);
    Window* add_window(const Rect& dims, const Rect* restore_rect, const std::string* title, std::uint16_t style,
                       int priority, bool draw);
    void draw_window(Window* window);
    void remove_window(Window* window, bool reanimate);
    void free_window(Window* window);
    void update_window(Window* window);

    [[nodiscard]] Port* port_by_id(std::uint16_t id) const;
    Port* set_port(Port* port);

    [[nodiscard]] Port* port() const { return current_; }

    void set_origin(int left, int top);
    void move_to(int left, int top);
    void move(int left, int top);
    void open_port(Port* port);

    void pen_color(int color) { current_->pen_color = static_cast<std::int16_t>(color); }

    void back_color(int color) { current_->back_color = static_cast<std::int16_t>(color); }

    void pen_mode(int mode) { current_->pen_mode = static_cast<std::int16_t>(mode); }

    void text_greyed_output(bool state) { current_->greyed_output = state; }

    [[nodiscard]] int point_size() const { return current_->font_height; }

    void offset_rect(Rect& r) const;
    void offset_line(GPoint& start, GPoint& end) const;
    void clip_line(GPoint& start, GPoint& end) const;

    void priority_bands_init(int band_count, int top, int bottom);
    void priority_bands_init(const std::array<std::uint8_t, 14>& data);

    void kernel_init_priority_bands() { priority_bands_init(14, 42, 190); }

    void kernel_graph_adjust_priority(int top, int bottom) { priority_bands_init(14, top, bottom); }

    [[nodiscard]] std::uint8_t kernel_coordinate_to_priority(int y) const;
    [[nodiscard]] int kernel_priority_to_coordinate(int priority) const;
    [[nodiscard]] std::vector<Reg> engine_hunks() const;

    [[nodiscard]] Window* pic_window() const { return pic_window_; }

    [[nodiscard]] Port* wmgr_port() const { return wmgr_port_.get(); }

    [[nodiscard]] Port* menu_port() const { return menu_port_.get(); }

    [[nodiscard]] const Rect& menu_bar_rect() const { return menu_bar_rect_; }

    [[nodiscard]] const Rect& menu_rect() const { return menu_rect_; }

    [[nodiscard]] const Rect& menu_line() const { return menu_line_; }

private:
    Graphics* graphics_;
    std::unique_ptr<Port> wmgr_port_;
    std::unique_ptr<Port> menu_port_;
    Window* pic_window_ = nullptr;
    Port* current_ = nullptr;
    Rect menu_bar_rect_;
    Rect menu_rect_;
    Rect menu_line_;
    std::list<Port*> windows_; // In visual order.
    std::vector<Port*> by_id_; // 0 and 1 are the window manager port.
    std::map<std::uint16_t, std::unique_ptr<Window>> owned_;
    std::vector<std::unique_ptr<Window>> zombies_; // Freed while current.
    std::uint16_t free_counter_ = 0;
    std::int16_t priority_top_ = 0;
    std::int16_t priority_bottom_ = 0;
    std::int16_t priority_band_count_ = 0;
    std::array<std::uint8_t, 200> priority_bands_{};
};

} // namespace sci::vm
