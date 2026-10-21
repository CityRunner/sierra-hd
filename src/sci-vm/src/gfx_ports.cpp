// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/ports.cpp (SCI0 late)
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

#include "gfx_ports.hpp"

#include "faults.hpp"
#include "graphics.hpp"
#include "machine.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace sci::vm {

Window* Ports::as_window(Port* port) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): ports with an id from 2 are windows.
    return static_cast<Window*>(port);
}

const Window* Ports::as_window(const Port* port) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): ports with an id from 2 are windows.
    return static_cast<const Window*>(port);
}

void Ports::init() {
    free_counter_ = 0;
    // The menu port has the fixed id 0xFFFF and is unknown to the window manager.
    menu_port_ = std::make_unique<Port>(0xFFFF);
    open_port(menu_port_.get());
    set_port(menu_port_.get());
    graphics_->text().set_font(0);
    menu_port_->rect = Rect{.top = 0, .left = 0, .bottom = Screen::height, .right = Screen::width};
    menu_bar_rect_ = Rect::from(0, 0, Screen::width, 9);
    menu_rect_ = Rect::from(0, 0, Screen::width, 10);
    menu_line_ = Rect::from(0, 9, Screen::width, 10);

    wmgr_port_ = std::make_unique<Port>(1);
    by_id_.assign(2, wmgr_port_.get());

    constexpr int off_top = 10;
    open_port(wmgr_port_.get());
    set_port(wmgr_port_.get());
    set_origin(0, off_top);
    wmgr_port_->rect.bottom = static_cast<std::int16_t>(Screen::height - off_top);
    wmgr_port_->rect.right = Screen::width;
    wmgr_port_->rect.move_to(0, 0);
    wmgr_port_->cur_top = 0;
    wmgr_port_->cur_left = 0;
    windows_.push_front(wmgr_port_.get());

    const Rect pic_rect{.top = off_top, .left = 0, .bottom = Screen::height, .right = Screen::width};
    pic_window_ = add_window(pic_rect, nullptr, nullptr, style_transparent | style_no_frame, 0, true);
    kernel_init_priority_bands();
}

void Ports::reset() {
    set_port(pic_window_);
    for (std::size_t id = first_script_window_id; id < by_id_.size(); ++id) {
        if (by_id_[id] != nullptr) {
            free_window(as_window(by_id_[id]));
        }
    }
    free_counter_ = 0;
    windows_.clear();
    windows_.push_front(wmgr_port_.get());
    windows_.push_back(pic_window_);
}

void Ports::kernel_set_active(std::uint16_t port_id) {
    if (free_counter_ != 0) {
        // Windows waiting to be freed.
        for (std::size_t id = first_script_window_id; id < by_id_.size(); ++id) {
            auto* window = as_window(by_id_[id]);
            if (window != nullptr && window->counter_till_free != 0) {
                --window->counter_till_free;
                if (window->counter_till_free == 0) {
                    free_window(window);
                    --free_counter_;
                }
            }
        }
    }
    switch (port_id) {
    case 0:
        set_port(wmgr_port_.get());
        break;
    case 0xFFFF:
        set_port(menu_port_.get());
        break;
    default:
        if (Port* port = port_by_id(port_id)) {
            set_port(port);
        } else {
            graphics_->faults().raise(
                std::format("GfxPorts::kernelSetActive was requested to set invalid port id {}", port_id));
        }
        break;
    }
}

void Ports::kernel_set_pic_window(const Rect& rect, std::int16_t pic_top, std::int16_t pic_left, bool init_bands) {
    pic_window_->rect = rect;
    pic_window_->top = pic_top;
    pic_window_->left = pic_left;
    if (init_bands) {
        kernel_init_priority_bands();
    }
}

Reg Ports::kernel_new_window(const Rect& dims, const Rect& restore_rect, std::uint16_t style, int priority,
                             int pen_color, int back_color, const std::string& title) {
    Window* window = nullptr;
    if (restore_rect.bottom != 0 && restore_rect.right != 0) {
        window = add_window(dims, &restore_rect, &title, style, priority, false);
    } else {
        window = add_window(dims, nullptr, &title, style, priority, false);
    }
    window->pen_color = static_cast<std::int16_t>(pen_color);
    window->back_color = static_cast<std::int16_t>(back_color);
    draw_window(window);
    return make_reg(0, window->id);
}

void Ports::kernel_dispose_window(std::uint16_t window_id, bool reanimate) {
    auto* window = as_window(port_by_id(window_id));
    if (window == nullptr) {
        graphics_->faults().raise(std::format("kDisposeWindow: used unknown window id {}", window_id));
        return;
    }
    if (window->counter_till_free != 0) {
        graphics_->faults().raise(std::format("kDisposeWindow: used already disposed window id {}", window_id));
        return;
    }
    remove_window(window, reanimate);
}

void Ports::begin_update(Window* window) {
    Port* old_port = set_port(wmgr_port_.get());
    const auto end = std::ranges::find(windows_, window);
    for (auto it = windows_.end(); it != end;) {
        --it;
        if (it == end) {
            break;
        }
        update_window(as_window(*it));
    }
    set_port(old_port);
}

void Ports::end_update(Window* window) {
    Port* old_port = set_port(wmgr_port_.get());
    auto it = std::ranges::find(windows_, window);
    if (it != windows_.end()) {
        for (++it; it != windows_.end(); ++it) {
            update_window(as_window(*it));
        }
    }
    // SCI0 also redraws the box (SCI_VERSION_1_EGA_ONLY and older).
    graphics_->paint().kernel_graph_redraw_box(current_->rect);
    set_port(old_port);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxPorts::addWindow.
Window* Ports::add_window(const Rect& dims, const Rect* restore_rect, const std::string* title, std::uint16_t style,
                          int priority, bool draw) {
    // An unused id.
    std::size_t id = first_window_id;
    while (id < by_id_.size() && by_id_[id] != nullptr) {
        if (by_id_[id]->counter_till_free != 0) {
            // Disposed but not freed yet: reuse the id.
            free_window(as_window(by_id_[id]));
            --free_counter_;
            break;
        }
        ++id;
    }
    if (id == by_id_.size()) {
        by_id_.push_back(nullptr);
    }
    auto owned = std::make_unique<Window>(static_cast<std::uint16_t>(id));
    Window* window = owned.get();
    owned_[static_cast<std::uint16_t>(id)] = std::move(owned);
    by_id_[id] = window;

    // SCI0 adds every window at the back.
    windows_.push_back(window);
    open_port(window);

    Rect r = dims;
    // Sierra cleared the last bit of the left edge.
    r.left = static_cast<std::int16_t>(static_cast<unsigned>(r.left) & 0xFFFEU);
    const std::uint16_t style_user_value = style_user | style_transparent;
    if (r.width() > Screen::width) {
        warn(std::format("Fixing too large window, left: {}, right: {}", dims.left, dims.right));
        r.left = 0;
        r.right = Screen::width - 1;
        if (style != style_user_value && (style & style_no_frame) == 0) {
            --r.right;
        }
    }
    window->rect = r;
    if (restore_rect != nullptr) {
        window->restore_rect = *restore_rect;
    }
    window->style = style;
    window->saved_visual = null_reg;
    window->saved_priority = null_reg;
    window->drawn = false;
    if ((style & style_transparent) == 0) {
        window->save_screen_mask = priority == -1 ? mask_visual : mask_visual | mask_priority;
    }
    if (title != nullptr && (style & style_title) != 0) {
        window->title = *title;
    }

    r = window->rect;
    if (style != style_user_value && (style & style_no_frame) == 0) {
        r.grow(1);
        if ((style & style_title) != 0) {
            r.top = static_cast<std::int16_t>(r.top - 10);
            ++r.bottom;
        }
    }
    window->dims = r;

    const Rect wmgr_rect = wmgr_port_->rect;
    const std::int16_t old_top = window->dims.top;
    const std::int16_t old_left = window->dims.left;
    if (wmgr_rect.top > window->dims.top) {
        window->dims.move_to(window->dims.left, wmgr_rect.top);
        if (restore_rect != nullptr) {
            window->restore_rect.move_to(window->restore_rect.left, wmgr_rect.top);
        }
    }
    if (wmgr_rect.bottom < window->dims.bottom) {
        window->dims.move_to(window->dims.left, wmgr_rect.bottom - window->dims.bottom + window->dims.top);
        if (restore_rect != nullptr) {
            window->restore_rect.move_to(window->restore_rect.left,
                                         wmgr_rect.bottom - window->restore_rect.bottom + window->restore_rect.top);
        }
    }
    if (wmgr_rect.right < window->dims.right) {
        window->dims.move_to(wmgr_rect.right + window->dims.left - window->dims.right, window->dims.top);
        if (restore_rect != nullptr) {
            window->restore_rect.move_to(wmgr_rect.right + window->restore_rect.left - window->restore_rect.right,
                                         window->restore_rect.top);
        }
    }
    if (wmgr_rect.left > window->dims.left) {
        window->dims.move_to(wmgr_rect.left, window->dims.top);
        if (restore_rect != nullptr) {
            window->restore_rect.move_to(wmgr_rect.left, window->restore_rect.top);
        }
    }
    window->rect.move_to(window->rect.left + window->dims.left - old_left,
                         window->rect.top + window->dims.top - old_top);
    if (restore_rect == nullptr) {
        window->restore_rect = window->dims;
    }
    if (draw) {
        draw_window(window);
    }
    set_port(window);
    set_origin(window->rect.left, window->rect.top + wmgr_port_->top);
    window->rect.move_to(0, 0);
    return window;
}

void Ports::draw_window(Window* window) {
    if (window->drawn) {
        return;
    }
    const std::uint16_t style = window->style;
    window->drawn = true;
    Paint& paint = graphics_->paint();
    Port* old_port = set_port(wmgr_port_.get());
    pen_color(0);
    if ((style & style_transparent) == 0) {
        window->saved_visual = paint.bits_save(window->restore_rect, mask_visual);
        if ((window->save_screen_mask & mask_priority) != 0) {
            window->saved_priority = paint.bits_save(window->restore_rect, mask_priority);
            if ((style & style_user) == 0) {
                paint.fill_rect(window->restore_rect, mask_priority, 0, 15, 0);
            }
        }
    }
    // Frame, shadow and title.
    if (style != (style_user | style_transparent)) {
        Rect r = window->dims;
        if ((style & style_no_frame) == 0) {
            ++r.top;
            ++r.left;
            paint.frame_rect(r); // The shadow.
            r.translate(-1, -1);
            paint.frame_rect(r); // The frame.
            if ((style & style_title) != 0) {
                // A black line between the title bar and the content.
                r.bottom = static_cast<std::int16_t>(r.top + 10);
                paint.frame_rect(r);
                r.grow(-1);
                paint.fill_rect(r, mask_visual, 8, 0, 0); // The grey title bar.
                if (!window->title.empty()) {
                    const int old_color = current_->pen_color;
                    pen_color(Screen::color_white);
                    graphics_->text().box(window->title, true, r, text_align_center, 0);
                    pen_color(old_color);
                }
                r.grow(+1);
                r.bottom = static_cast<std::int16_t>(window->dims.bottom - 1);
                r.top = static_cast<std::int16_t>(r.top + 9);
            }
            r.grow(-1);
        }
        if ((style & style_transparent) == 0) {
            paint.fill_rect(r, mask_visual, static_cast<std::uint8_t>(window->back_color), 0, 0);
        }
        paint.bits_show(window->dims);
    }
    set_port(old_port);
}

void Ports::remove_window(Window* window, bool reanimate) {
    Paint& paint = graphics_->paint();
    set_port(wmgr_port_.get());
    paint.bits_restore(window->saved_visual);
    window->saved_visual = null_reg;
    paint.bits_restore(window->saved_priority);
    window->saved_priority = null_reg;
    if (!reanimate) {
        paint.bits_show(window->restore_rect);
    } else {
        paint.kernel_graph_redraw_box(window->restore_rect);
    }
    windows_.remove(window);
    set_port(windows_.back());
    // Freed after 15 kSetPort calls: some scripts use a window just disposed.
    window->counter_till_free = 15;
    ++free_counter_;
}

void Ports::free_window(Window* window) {
    const SegManager& segments = graphics_->machine().segments();
    if (!window->saved_visual.is_null()) {
        segments.free_hunk_entry(window->saved_visual);
    }
    if (!window->saved_priority.is_null()) {
        segments.free_hunk_entry(window->saved_priority);
    }
    const std::uint16_t id = window->id;
    by_id_[id] = nullptr;
    const auto found = owned_.find(id);
    if (current_ == window) {
        // ScummVM keeps using the freed port until another is set; keep it.
        zombies_.push_back(std::move(found->second));
    }
    owned_.erase(found);
}

void Ports::update_window(Window* window) {
    Paint& paint = graphics_->paint();
    if (window->save_screen_mask != 0 && window->drawn) {
        Reg handle = paint.bits_save(window->restore_rect, mask_visual);
        paint.bits_restore(window->saved_visual);
        window->saved_visual = handle;
        if ((window->save_screen_mask & mask_priority) != 0) {
            handle = paint.bits_save(window->restore_rect, mask_priority);
            paint.bits_restore(window->saved_priority);
            window->saved_priority = handle;
        }
    }
}

Port* Ports::port_by_id(std::uint16_t id) const {
    return id < by_id_.size() ? by_id_[id] : nullptr;
}

Port* Ports::set_port(Port* port) {
    Port* old = current_;
    current_ = port;
    return old;
}

void Ports::set_origin(int left, int top) {
    current_->left = static_cast<std::int16_t>(static_cast<unsigned>(left) & 0xFFFEU); // SSCI clears the last bit.
    current_->top = static_cast<std::int16_t>(top);
}

void Ports::move_to(int left, int top) {
    current_->cur_top = static_cast<std::int16_t>(top);
    current_->cur_left = static_cast<std::int16_t>(left);
}

void Ports::move(int left, int top) {
    current_->cur_top = static_cast<std::int16_t>(current_->cur_top + top);
    current_->cur_left = static_cast<std::int16_t>(current_->cur_left + left);
}

void Ports::open_port(Port* port) {
    port->font_id = 0;
    port->font_height = 8;
    Port* old = current_;
    current_ = port;
    graphics_->text().set_font(port->font_id);
    current_ = old;
    port->top = 0;
    port->left = 0;
    port->greyed_output = false;
    port->pen_color = 0;
    port->back_color = Screen::color_white;
    port->pen_mode = 0;
    port->rect = Rect{}; // GfxPorts::_bounds, never set.
}

void Ports::offset_rect(Rect& r) const {
    r.translate(current_->left, current_->top);
}

void Ports::offset_line(GPoint& start, GPoint& end) const {
    start.x = static_cast<std::int16_t>(start.x + current_->left);
    start.y = static_cast<std::int16_t>(start.y + current_->top);
    end.x = static_cast<std::int16_t>(end.x + current_->left);
    end.y = static_cast<std::int16_t>(end.y + current_->top);
}

void Ports::clip_line(GPoint& start, GPoint& end) const {
    const Rect& r = current_->rect;
    const auto clip = [](std::int16_t value, int low, int high) {
        return static_cast<std::int16_t>(std::clamp<int>(value, low, high));
    };
    start.y = clip(start.y, r.top, r.bottom - 1);
    start.x = clip(start.x, r.left, r.right - 1);
    end.y = clip(end.y, r.top, r.bottom - 1);
    end.x = clip(end.x, r.left, r.right - 1);
}

void Ports::priority_bands_init(int band_count, int top, int bottom) {
    if (band_count != -1) {
        priority_band_count_ = static_cast<std::int16_t>(band_count);
    }
    priority_top_ = static_cast<std::int16_t>(top);
    priority_bottom_ = static_cast<std::int16_t>(bottom);
    // Sierra computed the bands in 32-bit integers; keep this exact.
    const std::int32_t band_size = ((priority_bottom_ - priority_top_) * 2000) / priority_band_count_;
    std::fill_n(priority_bands_.begin(), std::clamp<int>(priority_top_, 0, 200), 0);
    for (int y = priority_top_; y < priority_bottom_; ++y) {
        priority_bands_[static_cast<std::size_t>(y)] =
            static_cast<std::uint8_t>(1 + (((y - priority_top_) * 2000) / band_size));
    }
    if (priority_band_count_ == 15) {
        // Band 15 becomes band 14, as in Sierra's interpreter.
        int y = priority_bottom_;
        while (std::cmp_equal(priority_bands_[static_cast<std::size_t>(--y)], priority_band_count_)) {
            --priority_bands_[static_cast<std::size_t>(y)];
        }
    }
    for (int y = priority_bottom_; y < 200; ++y) {
        priority_bands_[static_cast<std::size_t>(y)] = static_cast<std::uint8_t>(priority_band_count_);
    }
    if (priority_bottom_ == 200) {
        --priority_bottom_;
    }
}

void Ports::priority_bands_init(const std::array<std::uint8_t, 14>& data) {
    std::size_t i = 0;
    std::size_t index = 0;
    for (index = 0; index < 14; ++index) {
        const std::uint8_t priority = data[index];
        while (i < priority && i < 200) {
            priority_bands_[i++] = static_cast<std::uint8_t>(index);
        }
    }
    while (i < 200) {
        priority_bands_[i++] = static_cast<std::uint8_t>(index);
    }
}

std::uint8_t Ports::kernel_coordinate_to_priority(int y) const {
    if (y < 0) {
        return priority_bands_[0];
    }
    if (y > priority_bottom_) {
        return priority_bands_[static_cast<std::size_t>(priority_bottom_)];
    }
    return priority_bands_[static_cast<std::size_t>(y)];
}

int Ports::kernel_priority_to_coordinate(int priority_value) const {
    // kernelPriorityToCoordinate takes a byte.
    const auto priority = static_cast<int>(static_cast<unsigned>(priority_value) & 0xFFU);
    if (priority <= priority_band_count_) {
        for (int y = 0; y <= priority_bottom_; ++y) {
            if (std::cmp_equal(priority_bands_[static_cast<std::size_t>(y)], priority)) {
                return y;
            }
        }
    }
    return priority_bottom_;
}

std::vector<Reg> Ports::engine_hunks() const {
    std::vector<Reg> hunks;
    for (const Port* port : windows_) {
        if (port->is_window()) {
            const Window* window = as_window(port);
            hunks.push_back(window->saved_visual);
            hunks.push_back(window->saved_priority);
        }
    }
    return hunks;
}

} // namespace sci::vm
