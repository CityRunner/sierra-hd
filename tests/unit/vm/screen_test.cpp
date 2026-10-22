// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screen's bookkeeping for the HD composition (docs/hd-composition.md):
// the owner map, the priority source map, the background layer and the
// sprite lists, on synthetic drawing.

#include "gfx_screen.hpp"
#include "sci_test.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

using sci::vm::GPoint;
using sci::vm::mask_all;
using sci::vm::mask_priority;
using sci::vm::mask_visual;
using sci::vm::Owner;
using sci::vm::OwnerScope;
using sci::vm::Rect;
using sci::vm::Screen;
using sci::vm::ShownSprite;

namespace {

std::size_t at(int x, int y) {
    return (static_cast<std::size_t>(y) * Screen::width) + static_cast<std::size_t>(x);
}

const Rect whole = Rect::from(0, 0, Screen::width, Screen::height);

ShownSprite sprite_at(int left, int top) {
    return ShownSprite{
        .view = 1,
        .loop = 0,
        .cel = 0,
        .left = left,
        .top = top,
        .width = 4,
        .height = 4,
        .pixels = {},
        .clear_key = 0,
        .priority = 8,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = Screen::width,
        .clip_bottom = Screen::height,
    };
}

} // namespace

SCI_TEST(pixels_carry_the_owner_of_the_drawing_scope) {
    Screen screen;
    SCI_CHECK(screen.owner(5, 5) == Owner::Interface);
    {
        const OwnerScope scope(screen, Owner::Sprite);
        screen.put_pixel(5, 20, mask_visual, 3, 0, 0);
        {
            const OwnerScope inner(screen, Owner::AddToPic);
            screen.put_pixel(6, 20, mask_visual, 3, 0, 0);
        }
        screen.put_pixel(7, 20, mask_visual, 3, 0, 0);
        screen.put_pixel(8, 20, mask_priority, 0, 9, 0); // Priority only: the owner stays.
    }
    screen.put_pixel(9, 20, mask_visual, 3, 0, 0);
    SCI_CHECK(screen.owner(5, 20) == Owner::Sprite);
    SCI_CHECK(screen.owner(6, 20) == Owner::AddToPic);
    SCI_CHECK(screen.owner(7, 20) == Owner::Sprite);
    SCI_CHECK(screen.owner(8, 20) == Owner::Interface);
    SCI_CHECK(screen.owner(9, 20) == Owner::Interface);
}

SCI_TEST(pictures_own_their_port_and_set_its_priority_source) {
    Screen screen;
    screen.put_pixel(10, 50, mask_all, 4, 7, 0);
    SCI_CHECK(screen.priority_source(10, 50) == sci::vm::priority_from_drawing);
    screen.picture_drawn();
    SCI_CHECK(screen.owner(10, 50) == Owner::Picture);
    SCI_CHECK(screen.priority_source(10, 50) == sci::vm::priority_from_picture);
    SCI_CHECK(screen.owner(10, 5) == Owner::Interface); // The menu bar is not the picture's.
    // An addToPic cel writes visual and priority over the picture.
    {
        const OwnerScope scope(screen, Owner::AddToPic);
        screen.put_pixel(20, 60, mask_visual | mask_priority, 5, 11, 0);
    }
    SCI_CHECK(screen.owner(20, 60) == Owner::AddToPic);
    SCI_CHECK(screen.priority_source(20, 60) == sci::vm::priority_from_drawing);
    // An overlay owns what it changes, and its priorities are the picture's.
    const std::vector<std::uint8_t> visual_before = screen.visual_map();
    const std::vector<std::uint8_t> priority_before = screen.priority_map();
    screen.visual_map()[at(20, 60)] = 6;
    screen.priority_map()[at(21, 60)] = 3;
    screen.overlay_drawn(visual_before, priority_before);
    SCI_CHECK(screen.owner(20, 60) == Owner::Picture);
    SCI_CHECK(screen.priority_source(20, 60) == sci::vm::priority_from_drawing); // Its priority is unchanged.
    SCI_CHECK(screen.priority_source(21, 60) == sci::vm::priority_from_picture);
}

SCI_TEST(saved_bits_carry_owner_and_priority_source) {
    Screen screen;
    screen.picture_drawn();
    const Rect rect = Rect::from(10, 20, 14, 24);
    // ScummVM's layout plus the priority source plane.
    SCI_CHECK(Screen::bits_data_size(rect, mask_all) == 9 + (16 * 5));
    SCI_CHECK(Screen::bits_data_size(rect, mask_visual) == 9 + (16 * 2));
    std::vector<std::uint8_t> saved(Screen::bits_data_size(rect, mask_all));
    screen.bits_save(rect, mask_all, saved);
    {
        const OwnerScope scope(screen, Owner::Sprite);
        screen.put_pixel(11, 21, mask_visual | mask_priority, 2, 9, 0);
    }
    SCI_CHECK(screen.owner(11, 21) == Owner::Sprite);
    screen.bits_restore(saved);
    SCI_CHECK(screen.owner(11, 21) == Owner::Picture);
    SCI_CHECK(screen.priority_source(11, 21) == sci::vm::priority_from_picture);
    SCI_CHECK(screen.visual(11, 21) == 0 && screen.priority(11, 21) == 0);
}

SCI_TEST(the_background_layer_is_the_screen_before_the_sprites) {
    Screen screen;
    screen.picture_drawn();
    screen.put_pixel(30, 40, mask_visual, 6, 0, 0);
    screen.begin_sprites();
    {
        const OwnerScope scope(screen, Owner::Sprite);
        screen.put_pixel(30, 40, mask_visual | mask_priority, 14, 8, 0);
    }
    screen.add_sprite(sprite_at(28, 38));
    screen.show(Rect::from(20, 30, 40, 50));
    screen.end_sprites();
    SCI_CHECK(screen.shown()[at(30, 40)] == 14);
    SCI_CHECK(screen.shown_owner()[at(30, 40)] == static_cast<std::uint8_t>(Owner::Sprite));
    SCI_CHECK(screen.shown_background_visual()[at(30, 40)] == 6);
    SCI_CHECK(screen.shown_background_owner()[at(30, 40)] == static_cast<std::uint8_t>(Owner::Interface));
    SCI_CHECK(screen.shown_background_priority()[at(30, 40)] == 0);
    SCI_CHECK(screen.shown_background_source()[at(30, 40)] == sci::vm::priority_from_picture);
    SCI_CHECK(screen.shown_background_owner()[at(25, 45)] == static_cast<std::uint8_t>(Owner::Picture));
    // Shown pixels name the sprite list; others none.
    const std::uint32_t generation = screen.shown_sprite_layer()[at(30, 40)];
    SCI_CHECK(generation != 0);
    SCI_CHECK(screen.shown_sprite_layer()[at(50, 40)] == 0);
    SCI_CHECK(screen.sprite_lists().size() == 1);
    SCI_CHECK(screen.sprite_lists().front().generation == generation);
    SCI_CHECK(screen.sprite_lists().front().sprites.size() == 1);
}

SCI_TEST(pixels_shown_between_cycles_lose_their_sprites) {
    Screen screen;
    screen.begin_sprites();
    screen.add_sprite(sprite_at(10, 20));
    screen.show(whole);
    screen.end_sprites();
    const std::uint32_t generation = screen.shown_sprite_layer()[at(10, 20)];
    SCI_CHECK(generation != 0);
    // Text shown between two kAnimate cycles: the sprites are gone there
    // (the visual map holds the background again), kept elsewhere.
    screen.show(Rect::from(0, 0, 12, 200));
    SCI_CHECK(screen.shown_sprite_layer()[at(10, 20)] == 0);
    SCI_CHECK(screen.shown_sprite_layer()[at(12, 20)] == generation);
    SCI_CHECK(screen.shown_background_visual()[at(10, 20)] == screen.visual(10, 20));
}

SCI_TEST(an_unchanged_sprite_list_keeps_its_generation_and_old_lists_go) {
    Screen screen;
    const std::uint64_t serial = screen.shown_serial();
    screen.begin_sprites();
    screen.add_sprite(sprite_at(10, 20));
    screen.show(whole);
    screen.end_sprites();
    const std::uint32_t first = screen.shown_sprite_layer()[at(10, 20)];
    const std::uint64_t after_first = screen.shown_serial();
    SCI_CHECK(after_first > serial);
    // The same cels again: nothing shown changes.
    screen.begin_sprites();
    screen.add_sprite(sprite_at(10, 20));
    screen.show(whole);
    screen.end_sprites();
    SCI_CHECK(screen.shown_sprite_layer()[at(10, 20)] == first);
    SCI_CHECK(screen.shown_serial() == after_first);
    // A moved cel shown everywhere: a new list, and the old one is dropped
    // when the next list comes.
    screen.begin_sprites();
    screen.add_sprite(sprite_at(11, 20));
    screen.show(whole);
    screen.end_sprites();
    const std::uint32_t second = screen.shown_sprite_layer()[at(10, 20)];
    SCI_CHECK(second != first);
    screen.begin_sprites();
    screen.add_sprite(sprite_at(12, 20));
    screen.show(Rect::from(0, 0, 100, 100));
    screen.end_sprites();
    SCI_CHECK(screen.sprite_lists().size() == 2); // The second (still shown below row 100) and the third.
    SCI_CHECK(screen.sprite_lists().front().generation == second);
    // No cels: pixels shown name no list.
    screen.begin_sprites();
    screen.show(whole);
    screen.end_sprites();
    SCI_CHECK(screen.shown_sprite_layer()[at(10, 20)] == 0);
}

SCI_TEST(kernel_lines_draw_with_the_line_owner) {
    Screen screen;
    screen.picture_drawn();
    {
        const OwnerScope scope(screen, Owner::Line);
        screen.draw_line(GPoint{.x = 10, .y = 30}, GPoint{.x = 20, .y = 30}, 4, 255, 255);
    }
    SCI_CHECK(screen.owner(15, 30) == Owner::Line);
    SCI_CHECK(screen.priority_source(15, 30) == sci::vm::priority_from_picture); // No priority drawn.
    SCI_CHECK(screen.owner(15, 31) == Owner::Picture);
}
