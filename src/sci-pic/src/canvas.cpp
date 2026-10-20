// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/screen.cpp (GfxScreen::dither,
//       GfxScreen::vectorIsFillMatch), engines/sci/graphics/picture.cpp
//       (GfxPicture::vectorFloodFill)
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

#include "canvas.hpp"

#include "sci/pic/geometry.hpp"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

namespace sci::pic::detail {

void Canvas::draw_line(int x0, int y0, int x1, int y1, std::uint8_t color, std::uint8_t priority,
                       std::uint8_t control) {
    const std::uint8_t mask = drawing_mask(color, priority, control);
    for_each_line_pixel(ScreenPoint{.x = x0, .y = y0}, ScreenPoint{.x = x1, .y = y1},
                        [&](int x, int y) { put(x, y, mask, color, priority, control); });
}

void Canvas::dither() noexcept {
    for (int y = 0; y < screen_height; ++y) {
        for (int x = 0; x < screen_width; ++x) {
            std::uint8_t& value = maps_->visual[PictureMaps::index(x, y)];
            if ((value & 0xF0U) != 0) {
                value = pair_color_at(decode_pair(value), x, y);
            }
        }
    }
}

namespace {

// The picture port: rows picture_top to 199, the full width. Fills stop at
// its edges.
constexpr int border_left = 0;
constexpr int border_top = picture_top;
constexpr int border_right = screen_width - 1;
constexpr int border_bottom = screen_height - 1;

// What a fill replaces: the map it tests (the first one it draws into) and
// the values found at the start point.
struct FillTarget {
    std::uint8_t match_mask = 0;
    std::uint8_t color = 0;
    std::uint8_t priority = 0;
    std::uint8_t control = 0;

    // GfxScreen::vectorIsFillMatch for EGA (the visual map compares the
    // visible colour).
    [[nodiscard]] bool matches(const Canvas& canvas, int x, int y) const noexcept {
        if ((match_mask & mask_visual) != 0) {
            return visible_color(canvas.code(x, y), x, y) == color;
        }
        if ((match_mask & mask_priority) != 0) {
            return canvas.priority(x, y) == priority;
        }
        return canvas.control(x, y) == control;
    }
};

struct FillPoint {
    int x = 0;
    int y = 0;
};

struct Brush {
    std::uint8_t mask = 0;
    std::uint8_t color = 0;
    std::uint8_t priority = 0;
    std::uint8_t control = 0;
};

// Queues the start of each run of matching pixels in the rows above and
// below the filled span [left, right], one point per run. The two rows are
// scanned together, alternating at each x, so that the points are pushed in
// exactly ScummVM's order.
void queue_runs(const Canvas& canvas, const FillTarget& target, int left, int right, int y,
                std::vector<FillPoint>& stack) {
    bool above_queued = false;
    bool below_queued = false;
    for (int x = left; x <= right; ++x) {
        const bool above = y > border_top && target.matches(canvas, x, y - 1);
        if (above && !above_queued) {
            stack.push_back(FillPoint{.x = x, .y = y - 1});
        }
        above_queued = above;
        const bool below = y < border_bottom && target.matches(canvas, x, y + 1);
        if (below && !below_queued) {
            stack.push_back(FillPoint{.x = x, .y = y + 1});
        }
        below_queued = below;
    }
}

// The span fill of vectorFloodFill: fill left and right from the point, then
// queue the first matching pixel of each run above and below.
void fill_from(Canvas& canvas, FillPoint start, const FillTarget& target, const Brush& brush) {
    std::vector<FillPoint> stack{start};
    while (!stack.empty()) {
        const FillPoint p = stack.back();
        stack.pop_back();
        if (!target.matches(canvas, p.x, p.y)) {
            continue; // Already filled.
        }
        canvas.put(p.x, p.y, brush.mask, brush.color, brush.priority, brush.control);
        int to_left = p.x;
        int to_right = p.x;
        while (to_left > border_left && target.matches(canvas, to_left - 1, p.y)) {
            canvas.put(--to_left, p.y, brush.mask, brush.color, brush.priority, brush.control);
        }
        while (to_right < border_right && target.matches(canvas, to_right + 1, p.y)) {
            canvas.put(++to_right, p.y, brush.mask, brush.color, brush.priority, brush.control);
        }
        queue_runs(canvas, target, to_left, to_right, p.y, stack);
    }
}

// Sierra's rules for aborting a fill: it only spreads over white (visual) or
// over 0 (priority, control), and never with white or 0 itself. Only the
// first map in the mask is tested.
bool aborts(std::uint8_t mask, const FillTarget& found, std::uint8_t color, std::uint8_t priority,
            std::uint8_t control) noexcept {
    if ((mask & mask_visual) != 0) {
        return color == 15 || found.color != 15;
    }
    if ((mask & mask_priority) != 0) {
        return priority == 0 || found.priority != 0;
    }
    if ((mask & mask_control) != 0) {
        return control == 0 || found.control != 0;
    }
    return false;
}

// The mask without the maps that already hold the value at the start point.
std::uint8_t remaining_maps(std::uint8_t mask, const FillTarget& found, std::uint8_t color, std::uint8_t priority,
                            std::uint8_t control) noexcept {
    unsigned remaining = mask;
    if (found.color == color) {
        remaining &= ~unsigned{mask_visual};
    }
    if (found.priority == priority) {
        remaining &= ~unsigned{mask_priority};
    }
    if (found.control == control) {
        remaining &= ~unsigned{mask_control};
    }
    return static_cast<std::uint8_t>(remaining);
}

// The map a fill tests: the first one it draws into.
std::uint8_t first_map(std::uint8_t mask) noexcept {
    if ((mask & mask_visual) != 0) {
        return mask_visual;
    }
    if ((mask & mask_priority) != 0) {
        return mask_priority;
    }
    return mask_control;
}

} // namespace

bool flood_fill(Canvas& canvas, int x, int y, std::uint8_t color, std::uint8_t priority, std::uint8_t control) {
    const FillPoint start{.x = x, .y = y + picture_top};
    if (!Canvas::on_screen(start.x, start.y)) {
        return false;
    }
    std::uint8_t mask = drawing_mask(color, priority, control);
    // ScummVM takes the parity from the picture coordinates; picture_top is
    // even, so this is the parity of the screen position as well.
    const FillTarget found{
        .match_mask = 0,
        .color = visible_color(canvas.code(start.x, start.y), x, y),
        .priority = canvas.priority(start.x, start.y),
        .control = canvas.control(start.x, start.y),
    };

    if (aborts(mask, found, color, priority, control)) {
        return true;
    }
    mask = remaining_maps(mask, found, color, priority, control);
    if (mask == 0) {
        return true;
    }

    FillTarget target = found;
    target.match_mask = first_map(mask);
    fill_from(canvas, start, target, Brush{.mask = mask, .color = color, .priority = priority, .control = control});
    return true;
}

} // namespace sci::pic::detail
