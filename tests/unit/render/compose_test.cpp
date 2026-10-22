// SPDX-License-Identifier: GPL-3.0-or-later
//
// The composition of the game screen (docs/hd-composition.md) on the
// synthetic screen of composition_fixture.hpp.

#include "composition_fixture.hpp"
#include "sci/render/colors.hpp"
#include "sci/render/compose.hpp"
#include "sci_test.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

using sci::render::Rgb8;
using sci::test::CompositionFixture;
using sci::test::FixtureRect;

namespace {

constexpr int scale = 3;
constexpr int sub_pixels = scale * scale;

Rgb8 frame_pixel(const std::vector<std::uint8_t>& frame, int x, int y) {
    const std::size_t i =
        ((static_cast<std::size_t>(y) * CompositionFixture::width * scale) + static_cast<std::size_t>(x)) * 4;
    return {.r = frame[i], .g = frame[i + 1], .b = frame[i + 2]};
}

// The sub-pixels of the 1x pixel (x, y) in `color`.
int count(const std::vector<std::uint8_t>& frame, int x, int y, std::uint8_t color) {
    int found = 0;
    for (int dy = 0; dy < scale; ++dy) {
        for (int dx = 0; dx < scale; ++dx) {
            if (frame_pixel(frame, (x * scale) + dx, (y * scale) + dy) == sci::render::ega_palette[color]) {
                ++found;
            }
        }
    }
    return found;
}

bool same_block(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, int x, int y) {
    for (int dy = 0; dy < scale; ++dy) {
        for (int dx = 0; dx < scale; ++dx) {
            if (!(frame_pixel(a, (x * scale) + dx, (y * scale) + dy) ==
                  frame_pixel(b, (x * scale) + dx, (y * scale) + dy))) {
                return false;
            }
        }
    }
    return true;
}

// Whether the 1x priority changes next to (x, y): an occlusion edge.
bool near_priority_edge(const CompositionFixture& fixture, int x, int y) {
    const std::uint8_t here = fixture.maps().priority[CompositionFixture::at(x, y)];
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (fixture.maps().priority[CompositionFixture::at(x + dx, y + dy)] != here) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

SCI_TEST(snapped_priority_hides_sprites_in_the_1x_blocks) {
    const CompositionFixture fixture(scale);
    SCI_CHECK(fixture.drawn());
    const auto frame = fixture.compose(true);
    for (const FixtureRect& rect : {CompositionFixture::sprite, CompositionFixture::second_sprite}) {
        const std::uint8_t color = rect.left == CompositionFixture::sprite.left
                                       ? CompositionFixture::sprite_color
                                       : CompositionFixture::second_sprite_color;
        for (int y = rect.top; y < rect.bottom; ++y) {
            for (int x = rect.left; x < rect.right; ++x) {
                if (CompositionFixture::window.contains(x, y)) {
                    continue;
                }
                const bool drawn_at_1x = fixture.visual()[CompositionFixture::at(x, y)] == color;
                SCI_CHECK(count(frame, x, y, color) == (drawn_at_1x ? sub_pixels : 0));
            }
        }
    }
}

SCI_TEST(hd_priority_hides_sprites_along_the_vector_edge) {
    const CompositionFixture fixture(scale);
    const auto hd = fixture.compose(false);
    const auto snapped = fixture.compose(true);
    int partial = 0;
    int differing = 0;
    const FixtureRect& rect = CompositionFixture::sprite;
    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left + 1; x < rect.right; ++x) {
            if (CompositionFixture::window.contains(x, y)) {
                continue;
            }
            const int found = count(hd, x, y, CompositionFixture::sprite_color);
            partial += found > 0 && found < sub_pixels ? 1 : 0;
            if (count(snapped, x, y, CompositionFixture::sprite_color) != found) {
                ++differing;
                // Only along the occlusion edge.
                SCI_CHECK(near_priority_edge(fixture, x, y));
            }
        }
    }
    // The edge runs through pixels: some are partly hidden.
    SCI_CHECK(partial > 10);
    SCI_CHECK(differing >= partial);
}

SCI_TEST(the_interface_stays_on_top_of_sprites) {
    const CompositionFixture fixture(scale);
    for (const bool snap : {false, true}) {
        const auto frame = fixture.compose(snap);
        const FixtureRect& window = CompositionFixture::window;
        for (int y = window.top; y < window.bottom; ++y) {
            for (int x = window.left; x < window.right; ++x) {
                SCI_CHECK(count(frame, x, y, CompositionFixture::window_color) == sub_pixels);
            }
        }
    }
}

SCI_TEST(add_to_pic_priority_hides_sprites_in_1x_blocks) {
    const CompositionFixture fixture(scale);
    const auto hd = fixture.compose(false);
    const auto snapped = fixture.compose(true);
    const FixtureRect& block = CompositionFixture::add_to_pic;
    for (int y = block.top; y < block.bottom; ++y) {
        for (int x = block.left; x < block.right; ++x) {
            // The addToPic cel is its 1x block; the sprite behind it does
            // not show, not even in part.
            SCI_CHECK(count(hd, x, y, CompositionFixture::add_to_pic_color) == sub_pixels);
        }
    }
    const FixtureRect& rect = CompositionFixture::second_sprite;
    for (int y = rect.top; y < rect.bottom; ++y) {
        for (int x = rect.left; x < rect.right; ++x) {
            SCI_CHECK(same_block(hd, snapped, x, y));
        }
    }
}

SCI_TEST(original_mode_shows_the_shown_screen_in_blocks) {
    const CompositionFixture fixture(scale);
    sci::render::ComposeInput input = fixture.input(false);
    input.hd = false;
    std::vector<std::uint8_t> frame(CompositionFixture::pixels * sub_pixels * 4);
    sci::render::compose(input, scale, {.x = 0, .y = 0, .width = 320, .height = 200}, frame);
    for (int y = 0; y < CompositionFixture::height; y += 7) {
        for (int x = 0; x < CompositionFixture::width; x += 5) {
            SCI_CHECK(count(frame, x, y, fixture.visual()[CompositionFixture::at(x, y)]) == sub_pixels);
        }
    }
}

SCI_TEST(composing_a_rectangle_changes_only_its_blocks) {
    const CompositionFixture fixture(scale);
    const auto full = fixture.compose(false);
    std::vector<std::uint8_t> part(full.size(), 0);
    const sci::render::PixelRect rect{.x = 130, .y = 45, .width = 40, .height = 50};
    sci::render::compose(fixture.input(false), scale, rect, part);
    for (int y = 0; y < CompositionFixture::height; y += 3) {
        for (int x = 0; x < CompositionFixture::width; x += 3) {
            const bool inside = x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
            const Rgb8 got = frame_pixel(part, x * scale, y * scale);
            if (inside) {
                SCI_CHECK(same_block(part, full, x, y));
            } else {
                SCI_CHECK(got == Rgb8{});
            }
        }
    }
}

SCI_TEST(a_sprite_list_draws_only_where_its_pixels_were_shown_with_it) {
    const CompositionFixture fixture(scale);
    sci::render::ComposeInput input = fixture.input(false);
    // The pixels shown again without the sprites (text shown between two
    // animation cycles): no sprite there.
    std::vector<std::uint32_t> layer = fixture.sprite_layer();
    for (int y = 90; y < 110; ++y) {
        for (int x = 140; x < 180; ++x) {
            layer[CompositionFixture::at(x, y)] = 0;
        }
    }
    input.sprite_layer = layer;
    std::vector<std::uint8_t> frame(CompositionFixture::pixels * sub_pixels * 4);
    sci::render::compose(input, scale, {.x = 0, .y = 0, .width = 320, .height = 200}, frame);
    for (int y = 90; y < 110; ++y) {
        for (int x = 140; x < 180; ++x) {
            SCI_CHECK(count(frame, x, y, CompositionFixture::sprite_color) == 0);
        }
    }
    SCI_CHECK(count(frame, 150, 80, CompositionFixture::sprite_color) == sub_pixels);
}
