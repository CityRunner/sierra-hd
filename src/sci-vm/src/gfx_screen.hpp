// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/screen.{h,cpp} (SCI0 at 320x200, no upscaling)
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
#include "sci/vm/shown_screen.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sci::vm {

inline constexpr unsigned mask_visual = 1U;
inline constexpr unsigned mask_priority = 2U;
inline constexpr unsigned mask_control = 4U;
inline constexpr unsigned mask_all = mask_visual | mask_priority | mask_control;

// The visual, priority and control maps of the 320x200 screen. The display
// map ScummVM keeps beside the visual map holds the same colours here
// (dithering is not undone, the default of ScummVM).
//
// Beside them, for the HD composition (docs/hd-composition.md), the screen
// keeps the owner map (what drew each visual pixel), the priority source
// map, the background layer under the sprites while kAnimate draws them,
// and the sprite lists; and of each of these what is shown.
class Screen {
public:
    static constexpr int width = 320;
    static constexpr int height = 200;
    static constexpr std::uint8_t color_white = 15;

    Screen();

    void put_pixel(int x, int y, std::uint8_t draw_mask, std::uint8_t color, std::uint8_t priority,
                   std::uint8_t control);

    [[nodiscard]] std::uint8_t visual(int x, int y) const { return visual_[index(x, y)]; }

    [[nodiscard]] std::uint8_t priority(int x, int y) const { return priority_[index(x, y)]; }

    [[nodiscard]] std::uint8_t control(int x, int y) const { return control_[index(x, y)]; }

    [[nodiscard]] Owner owner(int x, int y) const { return static_cast<Owner>(owner_[index(x, y)]); }

    [[nodiscard]] std::uint8_t priority_source(int x, int y) const { return source_[index(x, y)]; }

    static std::uint8_t drawing_mask(std::uint8_t color, std::uint8_t priority, std::uint8_t control);
    void draw_line(GPoint start, GPoint end, std::uint8_t color, std::uint8_t priority, std::uint8_t control);

    // Saved bits: the rectangle, the mask, then the maps' pixels, laid out as
    // GfxScreen::bitsSave does. Where ScummVM saves its display map, a copy
    // of the visual map here, the owner map is saved instead (same size; the
    // scripts never read saved bits); the priority source map follows the
    // priority map (a plane ScummVM does not have).
    [[nodiscard]] static std::size_t bits_data_size(const Rect& rect, std::uint8_t mask);
    void bits_save(const Rect& rect, std::uint8_t mask, std::span<std::uint8_t> memory) const;
    [[nodiscard]] static Rect bits_rect(std::span<const std::uint8_t> memory);
    void bits_restore(std::span<const std::uint8_t> memory);

    // The display after a picture: the maps as the picture drew them.
    [[nodiscard]] std::vector<std::uint8_t>& visual_map() { return visual_; }

    [[nodiscard]] std::vector<std::uint8_t>& priority_map() { return priority_; }

    [[nodiscard]] std::vector<std::uint8_t>& control_map() { return control_; }

    // Who draws: put_pixel writes the current owner with every visual pixel,
    // and marks every priority it writes as drawn (not from a picture).
    [[nodiscard]] Owner current_owner() const { return current_owner_; }

    void set_current_owner(Owner owner) { current_owner_ = owner; }

    // A picture was drawn: the picture port (from row picture_top) is the
    // picture's, priorities included.
    void picture_drawn();
    // A picture overlay was drawn: the pixels it changed are the picture's.
    void overlay_drawn(std::span<const std::uint8_t> visual_before, std::span<const std::uint8_t> priority_before);

    // kAnimate draws its cels between these: the maps as they are at the
    // start are the background layer, which show() passes on while drawing
    // lasts, and the cels added make the next sprite list.
    void begin_sprites();
    void add_sprite(const ShownSprite& sprite);
    void end_sprites();

    [[nodiscard]] std::int16_t kernel_pic_not_valid(std::int16_t new_value);

    // What the player sees: ScummVM copies the display map to the screen
    // only in bitsShow and the transitions (copyRectToScreen). `rect` is in
    // screen coordinates. The owner map, the background layer and the sprite
    // list go along.
    void show(const Rect& rect);

    [[nodiscard]] const std::vector<std::uint8_t>& shown() const { return shown_; }

    [[nodiscard]] const std::vector<std::uint8_t>& shown_owner() const { return shown_owner_; }

    // The background layer of what is shown: visual, owner, priority and
    // priority source maps.
    [[nodiscard]] const std::vector<std::uint8_t>& shown_background_visual() const { return shown_bg_visual_; }

    [[nodiscard]] const std::vector<std::uint8_t>& shown_background_owner() const { return shown_bg_owner_; }

    [[nodiscard]] const std::vector<std::uint8_t>& shown_background_priority() const { return shown_bg_priority_; }

    [[nodiscard]] const std::vector<std::uint8_t>& shown_background_source() const { return shown_bg_source_; }

    // Per shown pixel, the generation of the sprite list it was shown with
    // (0: none); and those lists.
    [[nodiscard]] const std::vector<std::uint32_t>& shown_sprite_layer() const { return shown_layer_; }

    [[nodiscard]] const std::vector<SpriteList>& sprite_lists() const { return sprite_lists_; }

    // Counts the changes of what is shown.
    [[nodiscard]] std::uint64_t shown_serial() const { return shown_serial_; }

    // 0, 1 or 2: whether the picture shown is out of date.
    [[nodiscard]] int pic_not_valid() const { return pic_not_valid_; }

    void set_pic_not_valid(int value) { pic_not_valid_ = value; }

private:
    [[nodiscard]] static std::size_t index(int x, int y) {
        return (static_cast<std::size_t>(y) * width) + static_cast<std::size_t>(x);
    }

    // The generation the sprites drawn since begin_sprites are shown with.
    std::uint32_t promote_sprites();

    std::vector<std::uint8_t> visual_;
    std::vector<std::uint8_t> priority_;
    std::vector<std::uint8_t> control_;
    std::vector<std::uint8_t> owner_;
    std::vector<std::uint8_t> source_;
    Owner current_owner_ = Owner::Interface;

    // The background layer while sprites are drawn.
    bool drawing_sprites_ = false;
    std::vector<std::uint8_t> bg_visual_;
    std::vector<std::uint8_t> bg_owner_;
    std::vector<std::uint8_t> bg_priority_;
    std::vector<std::uint8_t> bg_source_;
    std::vector<ShownSprite> pending_sprites_;
    std::optional<std::uint32_t> pending_generation_; // Set when first shown.
    std::uint32_t next_generation_ = 1;
    std::vector<SpriteList> sprite_lists_; // The lists shown pixels name, oldest first.

    std::vector<std::uint8_t> shown_;
    std::vector<std::uint8_t> shown_owner_;
    std::vector<std::uint8_t> shown_bg_visual_;
    std::vector<std::uint8_t> shown_bg_owner_;
    std::vector<std::uint8_t> shown_bg_priority_;
    std::vector<std::uint8_t> shown_bg_source_;
    std::vector<std::uint32_t> shown_layer_;
    std::uint64_t shown_serial_ = 0;
    int pic_not_valid_ = 0;
};

// Sets the screen's current owner for a scope.
class OwnerScope {
public:
    OwnerScope(Screen& screen, Owner owner) : screen_(&screen), previous_(screen.current_owner()) {
        screen.set_current_owner(owner);
    }

    OwnerScope(const OwnerScope&) = delete;
    OwnerScope& operator=(const OwnerScope&) = delete;
    OwnerScope(OwnerScope&&) = delete;
    OwnerScope& operator=(OwnerScope&&) = delete;

    ~OwnerScope() { screen_->set_current_owner(previous_); }

private:
    Screen* screen_;
    Owner previous_;
};

} // namespace sci::vm
