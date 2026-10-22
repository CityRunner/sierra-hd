// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/paint16.cpp (SCI0 late)
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

#include "gfx_paint.hpp"

#include "graphics.hpp"
#include "machine.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"

#include <algorithm>
#include <format>

namespace sci::vm {

namespace {

constexpr std::uint16_t display_move_pen = 100;
constexpr std::uint16_t display_set_alignment = 101;
constexpr std::uint16_t display_set_pen_color = 102;
constexpr std::uint16_t display_set_background_color = 103;
constexpr std::uint16_t display_set_greyed_output = 104;
constexpr std::uint16_t display_set_font = 105;
constexpr std::uint16_t display_width = 106;
constexpr std::uint16_t display_save_under = 107;
constexpr std::uint16_t display_restore_under = 108;
constexpr std::uint16_t display_dont_show_bits = 121;

} // namespace

void Paint::draw_picture(int picture, bool mirrored, bool add_to, int ega_palette) {
    Screen& screen = graphics_->screen();
    Ports& ports = graphics_->ports();
    const Port* port = ports.port();
    if (port->top != pic::picture_top || port->left != 0 || port->rect != Rect::from(0, 0, Screen::width, 190)) {
        graphics_->faults().raise("kDrawPic: only the default picture window is supported");
        return;
    }
    const auto* data = graphics_->machine().resources().find(ResourceType::Pic, picture);
    if (data == nullptr) {
        graphics_->faults().raise(std::format("picture resource {} not found", picture));
        return;
    }
    auto commands = pic::parse_pic(*data);
    if (!commands) {
        graphics_->faults().raise(std::format("pic.{:03}: {}", picture, commands.error().message));
        return;
    }
    maps_.visual = screen.visual_map();
    maps_.priority = screen.priority_map();
    maps_.control = screen.control_map();
    // sci-pic clears the picture port first when not adding, as
    // GfxPaint16::clearScreen does.
    const auto drawn = pic::draw_picture(*commands, maps_,
                                         pic::DrawOptions{
                                             .mirrored = mirrored,
                                             .add_to = add_to,
                                             .ega_palette = static_cast<std::uint8_t>(ega_palette),
                                         });
    if (!drawn) {
        graphics_->faults().raise(std::format("pic.{:03}: {}", picture, drawn.error().message));
        return;
    }
    const std::vector<std::uint8_t> visual_before = add_to ? screen.visual_map() : std::vector<std::uint8_t>{};
    const std::vector<std::uint8_t> priority_before = add_to ? screen.priority_map() : std::vector<std::uint8_t>{};
    screen.visual_map() = maps_.visual;
    screen.priority_map() = maps_.priority;
    screen.control_map() = maps_.control;
    if (add_to) {
        screen.overlay_drawn(visual_before, priority_before);
    } else {
        screen.picture_drawn();
        drawn_stack_.clear();
        drawn_lines_.clear();
    }
    if (drawn->priority_bands) {
        ports.priority_bands_init(*drawn->priority_bands);
    }
    drawn_stack_.push_back(ShownPicture{.number = picture, .mirrored = mirrored, .ega_palette = ega_palette});
}

void Paint::draw_cel_and_show(int view, int loop, int cel, int left, int top, int priority, int /*palette*/) {
    const GfxView& found = graphics_->cache().view(view);
    const Rect cel_rect = Rect::from(left, top, left + found.width(loop, cel), top + found.height(loop, cel));
    draw_cel(found, loop, cel, cel_rect, priority);
    if (graphics_->screen().pic_not_valid() == 0) {
        bits_show(cel_rect);
    }
}

void Paint::draw_cel(const GfxView& view, int loop, int cel, const Rect& cel_rect, int priority) {
    const Ports& ports = graphics_->ports();
    Rect clip = cel_rect;
    clip.clip(ports.port()->rect);
    if (clip.is_empty()) {
        return;
    }
    Rect clip_translated = clip;
    ports.offset_rect(clip_translated);
    view.draw(graphics_->screen(), cel_rect, clip, clip_translated, loop, cel, static_cast<std::uint8_t>(priority));
}

void Paint::clear_screen(std::uint8_t color) {
    fill_rect(graphics_->ports().port()->rect, mask_all, color, 0, 0);
}

void Paint::invert_rect(const Rect& rect) {
    Port* port = graphics_->ports().port();
    const std::int16_t old_mode = port->pen_mode;
    port->pen_mode = 2;
    fill_rect(rect, mask_visual, static_cast<std::uint8_t>(port->pen_color),
              static_cast<std::uint8_t>(port->back_color), 0);
    port->pen_mode = old_mode;
}

void Paint::erase_rect(const Rect& rect) {
    fill_rect(rect, mask_visual, static_cast<std::uint8_t>(graphics_->ports().port()->back_color), 0, 0);
}

void Paint::paint_rect(const Rect& rect) {
    fill_rect(rect, mask_visual, static_cast<std::uint8_t>(graphics_->ports().port()->pen_color), 0, 0);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxPaint16::fillRect.
void Paint::fill_rect(const Rect& rect, int draw_flags, std::uint8_t color, std::uint8_t priority,
                      std::uint8_t control) {
    const Ports& ports = graphics_->ports();
    Screen& screen = graphics_->screen();
    Rect r = rect;
    r.clip(ports.port()->rect);
    if (r.is_empty()) {
        return;
    }
    const std::int16_t old_pen_mode = ports.port()->pen_mode;
    ports.offset_rect(r);
    if ((static_cast<unsigned>(draw_flags) & mask_visual) != 0) {
        for (int y = r.top; y < r.bottom; ++y) {
            for (int x = r.left; x < r.right; ++x) {
                if (old_pen_mode == 2) { // Invert.
                    const std::uint8_t current = screen.visual(x, y);
                    if (current == color) {
                        screen.put_pixel(x, y, mask_visual, priority, 0, 0);
                    } else if (current == priority) {
                        screen.put_pixel(x, y, mask_visual, color, 0, 0);
                    }
                } else {
                    screen.put_pixel(x, y, mask_visual, color, 0, 0);
                }
            }
        }
    }
    if (draw_flags < 2) {
        return;
    }
    const auto flags = static_cast<std::uint8_t>(static_cast<unsigned>(draw_flags) & (mask_priority | mask_control));
    // Sierra kept priority and control in one byte.
    priority &= 0x0FU;
    control &= 0x0FU;
    for (int y = r.top; y < r.bottom; ++y) {
        for (int x = r.left; x < r.right; ++x) {
            if (old_pen_mode != 2) {
                screen.put_pixel(x, y, flags, 0, priority, control);
            } else {
                screen.put_pixel(x, y, flags, 0, screen.priority(x, y) == 0 ? 1 : 0, screen.control(x, y) == 0 ? 1 : 0);
            }
        }
    }
}

void Paint::frame_rect(const Rect& rect) {
    Rect r = rect;
    r.right = static_cast<std::int16_t>(rect.left + 1); // Left.
    paint_rect(r);
    r.right = rect.right; // Right.
    r.left = static_cast<std::int16_t>(rect.right - 1);
    paint_rect(r);
    r.left = rect.left; // Top.
    r.bottom = static_cast<std::int16_t>(rect.top + 1);
    paint_rect(r);
    r.bottom = rect.bottom; // Bottom.
    r.top = static_cast<std::int16_t>(rect.bottom - 1);
    paint_rect(r);
}

void Paint::bits_show(const Rect& rect) {
    const Ports& ports = graphics_->ports();
    Rect r = rect;
    r.clip(ports.port()->rect);
    if (r.is_empty()) {
        return;
    }
    ports.offset_rect(r);
    // Even left and right edges (EGA bytes hold two pixels).
    r.left = static_cast<std::int16_t>(static_cast<unsigned>(r.left) & 0xFFFEU);
    r.right = static_cast<std::int16_t>((static_cast<unsigned>(r.right) + 1U) & 0xFFFEU);
    graphics_->screen().show(r);
    if (drawn_stack_ == shown_stack_) {
        shown_lines_ = drawn_lines_; // Kernel lines drawn into the picture shown.
    }
}

void Paint::picture_shown() {
    shown_stack_ = drawn_stack_;
    shown_lines_ = drawn_lines_;
}

Reg Paint::bits_save(const Rect& rect, std::uint8_t mask) {
    const Ports& ports = graphics_->ports();
    Rect r = rect;
    r.clip(ports.port()->rect);
    if (r.is_empty()) {
        return null_reg;
    }
    ports.offset_rect(r);
    SegManager& segments = graphics_->machine().segments();
    const Reg handle = segments.allocate_hunk_entry("SaveBits()", Screen::bits_data_size(r, mask));
    if (Hunk* hunk = segments.hunk(handle)) {
        graphics_->screen().bits_save(r, mask, hunk->mem);
    }
    return handle;
}

Rect Paint::bits_rect(Reg handle) const {
    if (!handle.is_null()) {
        if (const Hunk* hunk = graphics_->machine().segments().hunk(handle)) {
            return Screen::bits_rect(hunk->mem);
        }
    }
    return {};
}

void Paint::bits_restore(Reg handle) {
    if (handle.is_null()) {
        return;
    }
    if (const Hunk* hunk = graphics_->machine().segments().hunk(handle)) {
        graphics_->screen().bits_restore(hunk->mem);
        bits_free(handle);
    }
}

void Paint::bits_free(Reg handle) {
    if (!handle.is_null()) {
        graphics_->machine().segments().free_hunk_entry(handle);
    }
}

void Paint::kernel_draw_picture(int picture, int animation, bool blackout, bool mirrored, bool add_to,
                                int ega_palette) {
    Ports& ports = graphics_->ports();
    Port* old_port = ports.set_port(ports.pic_window());
    if (ports.is_front_window(ports.pic_window())) {
        graphics_->screen().set_pic_not_valid(1);
        draw_picture(picture, mirrored, add_to, ega_palette);
        graphics_->transitions().setup(animation, blackout);
    } else {
        ports.begin_update(ports.pic_window());
        draw_picture(picture, mirrored, add_to, ega_palette);
        ports.end_update(ports.pic_window());
        picture_shown(); // End_update redrew it under the windows.
    }
    ports.set_port(old_port);
}

void Paint::kernel_graph_draw_line(GPoint start, GPoint end, int color, int priority, int control) {
    const Ports& ports = graphics_->ports();
    ports.clip_line(start, end);
    ports.offset_line(start, end);
    Screen& screen = graphics_->screen();
    const bool in_picture = ports.port() == ports.pic_window();
    const OwnerScope owner(screen, in_picture ? Owner::Line : Owner::Interface);
    screen.draw_line(start, end, static_cast<std::uint8_t>(color), static_cast<std::uint8_t>(priority),
                     static_cast<std::uint8_t>(control));
    if (in_picture && (Screen::drawing_mask(static_cast<std::uint8_t>(color), 0, 0) & mask_visual) != 0) {
        // As Screen::draw_line clips it.
        drawn_lines_.push_back(ShownLine{
            .x0 = std::clamp<int>(start.x, 0, Screen::width - 1),
            .y0 = std::clamp<int>(start.y, 0, Screen::height - 1),
            .x1 = std::clamp<int>(end.x, 0, Screen::width - 1),
            .y1 = std::clamp<int>(end.y, 0, Screen::height - 1),
            .color = static_cast<std::uint8_t>(color),
        });
    }
}

Owner Paint::primitive_owner() const {
    const Ports& ports = graphics_->ports();
    return ports.port() == ports.pic_window() ? Owner::Primitive : Owner::Interface;
}

void Paint::kernel_graph_frame_box(const Rect& rect, int color) {
    Ports& ports = graphics_->ports();
    const int old_color = ports.port()->pen_color;
    ports.pen_color(color);
    frame_rect(rect);
    ports.pen_color(old_color);
}

void Paint::kernel_graph_redraw_box(Rect rect) {
    Ports& ports = graphics_->ports();
    // Local to global in the current port, then global to local in the
    // picture window.
    rect.translate(ports.port()->left, ports.port()->top);
    Port* old_port = ports.set_port(ports.pic_window());
    rect.translate(-ports.port()->left, -ports.port()->top);
    graphics_->animate().re_animate(rect);
    ports.set_port(old_port);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to GfxPaint16::kernelDisplay.
Reg Paint::kernel_display(const std::string& text, std::span<Reg> args) {
    Ports& ports = graphics_->ports();
    Text& text16 = graphics_->text();
    int alignment = text_align_left;
    int color_back = -1;
    int width = -1;
    bool redraw = true;
    bool save_under = false;
    Rect rect;
    Reg result = null_reg;

    // A copy of the port settings, restored at the end in SCI0 late.
    const Port old_port = *ports.port();
    ports.pen_mode(0);
    ports.pen_color(0);
    ports.text_greyed_output(false);
    std::size_t i = 0;
    const auto arg = [&](std::size_t at) { return at < args.size() ? args[at] : null_reg; };
    while (i < args.size()) {
        Reg code = args[i++];
        if (code.segment != 0) {
            code.offset = 0xFFFF;
        }
        switch (code.offset) {
        case display_move_pen:
            ports.move_to(arg(i).to_uint16(), arg(i + 1).to_uint16());
            i += 2;
            break;
        case display_set_alignment:
            alignment = arg(i++).to_sint16();
            break;
        case display_set_pen_color:
            ports.pen_color(arg(i++).to_uint16());
            break;
        case display_set_background_color:
            color_back = arg(i++).to_sint16();
            break;
        case display_set_greyed_output:
            ports.text_greyed_output(!arg(i++).is_null());
            break;
        case display_set_font:
            text16.set_font(arg(i++).to_uint16());
            break;
        case display_width:
            width = arg(i++).to_sint16();
            break;
        case display_save_under:
            save_under = true;
            break;
        case display_restore_under: {
            rect = bits_rect(arg(i));
            rect.translate(-ports.port()->left, -ports.port()->top);
            bits_restore(arg(i));
            kernel_graph_redraw_box(rect);
            i = args.size();
            break;
        }
        case display_dont_show_bits:
            redraw = false;
            break;
        default:
            // kDisplay_workarounds has no entries for this game.
            graphics_->faults().raise(
                std::format("Unknown kDisplay argument ({:04x}:{:04x})", code.segment, code.offset));
            return result;
        }
    }
    rect = text16.size(text, -1, width);
    rect.move_to(ports.port()->cur_left, ports.port()->cur_top);
    if (save_under) {
        result = bits_save(rect, mask_visual);
    }
    if (color_back != -1) {
        fill_rect(rect, mask_visual, static_cast<std::uint8_t>(color_back), 0, 0);
    }
    text16.box(text, false, rect, alignment, -1);
    if (graphics_->screen().pic_not_valid() == 0 && redraw) {
        bits_show(rect);
    }
    Port* current = ports.port();
    const std::int16_t top = current->cur_top;
    const std::int16_t left = current->cur_left;
    *current = old_port;
    current->cur_top = top;
    current->cur_left = left;
    return result;
}

} // namespace sci::vm
