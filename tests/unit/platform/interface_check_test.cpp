// SPDX-License-Identifier: GPL-3.0-or-later
//
// Windows render correctly at every N (milestone 6b): on the synthetic
// screen of tests/unit/render/composition_fixture.hpp (a window over a
// sprite, the menu bar), with an HD background whose every sub-pixel has
// another colour, the game's frame (sci/platform/game_frame.hpp) shows the
// interface as the 1x screen in N x N blocks at every N, in original and
// HD mode, and the picture around it does not change with it; and the
// checks of tools/common/interface_check.hpp, which
// sci-hd-frames runs on the scenarios, find a frame that breaks this.

#include "composition_fixture.hpp"
#include "interface_check.hpp"
#include "sci/platform/config.hpp"
#include "sci/platform/game_frame.hpp"
#include "sci/render/compose.hpp"
#include "sci/vm/game.hpp"
#include "sci_test.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

using sci::platform::GameFrame;
using sci::platform::GraphicsMode;
using sci::platform::HdImages;
using sci::test::CompositionFixture;

namespace {

// The fixture as the interpreter shows it.
struct Shown {
    explicit Shown(const CompositionFixture& fixture) : visual(fixture.visual()), owner(fixture.owner()) {
        sci::vm::SpriteList list{.generation = 1, .sprites = {}};
        for (const sci::render::ComposeSprite& sprite : fixture.sprites()) {
            list.sprites.push_back(sci::vm::ShownSprite{
                .view = 0,
                .loop = 0,
                .cel = 0,
                .left = sprite.left,
                .top = sprite.top,
                .width = sprite.width,
                .height = sprite.height,
                .pixels = sprite.pixels,
                .clear_key = sprite.clear_key,
                .priority = sprite.priority,
                .clip_left = sprite.clip_left,
                .clip_top = sprite.clip_top,
                .clip_right = sprite.clip_right,
                .clip_bottom = sprite.clip_bottom,
            });
        }
        lists = {list};
    }

    [[nodiscard]] sci::vm::ShownScreen screen(const CompositionFixture& fixture) const {
        return sci::vm::ShownScreen{
            .visual = visual,
            .serial = 1,
            .owner = owner,
            .background_visual = fixture.background_visual(),
            .background_owner = fixture.background_owner(),
            .background_priority = fixture.background_priority(),
            .background_source = fixture.background_source(),
            .sprite_layer = fixture.sprite_layer(),
            .sprite_lists = lists,
            .pictures = pictures,
            .lines = {},
            .cursor = -1,
            .mouse = sci::vm::Point{.x = 0, .y = 0},
        };
    }

    std::vector<sci::vm::SpriteList> lists;
    std::vector<std::uint8_t> visual;
    std::vector<std::uint8_t> owner;
    std::vector<sci::vm::ShownPicture> pictures{{.number = 1, .mirrored = false, .ega_palette = 0}};
};

// An HD background at N in which neighbouring sub-pixels differ, so that
// any of its sub-pixels in a window block shows; the HD priority is the 1x
// one.
sci::render::RgbaImage hd_background(int scale) {
    sci::render::RgbaImage image{
        .width = CompositionFixture::width * scale,
        .height = (CompositionFixture::height - sci::render::compose_picture_top) * scale,
        .pixels = {},
    };
    image.pixels.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t i =
                ((static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)) + static_cast<std::size_t>(x)) *
                4;
            const auto ux = static_cast<unsigned>(x);
            const auto uy = static_cast<unsigned>(y);
            image.pixels[i] = static_cast<std::uint8_t>((ux * 7U) & 0xFFU);
            image.pixels[i + 1] = static_cast<std::uint8_t>((uy * 13U) & 0xFFU);
            image.pixels[i + 2] = static_cast<std::uint8_t>((ux ^ uy) & 0xFFU);
            image.pixels[i + 3] = 0xFF;
        }
    }
    return image;
}

// The frame of the fixture's screen at N.
std::vector<std::uint8_t> compose(const sci::render::RgbaImage& background, const sci::vm::ShownScreen& shown,
                                  GraphicsMode mode, int scale) {
    GameFrame frame([&background](std::span<const sci::vm::ShownPicture> /*stack*/,
                                  std::span<const sci::vm::ShownLine> /*lines*/,
                                  int /*scale*/) { return HdImages{.background = &background, .priority = {}}; });
    frame.update(shown, mode, scale, false);
    return frame.frame();
}

constexpr int scale = 3;

} // namespace

SCI_TEST(the_interface_is_the_1x_screen_in_blocks_at_every_n) {
    const CompositionFixture fixture(1);
    SCI_CHECK(fixture.drawn());
    const Shown shown(fixture);
    for (int n = 1; n <= sci::platform::max_scale; ++n) {
        const sci::render::RgbaImage background = hd_background(n);
        const sci::vm::ShownScreen screen = shown.screen(fixture);
        SCI_CHECK(!sci::tools::check_menu_bar(screen));
        SCI_CHECK(sci::tools::window_over_sprite(screen));
        const auto original = compose(background, screen, GraphicsMode::Original, n);
        SCI_CHECK(!sci::tools::check_interface_blocks(screen, original, n, false));
        const auto hd = compose(background, screen, GraphicsMode::Hd, n);
        SCI_CHECK(!sci::tools::check_interface_blocks(screen, hd, n, true));
        // The window in other colours changes its blocks and nothing else.
        const std::vector<std::uint8_t> visual = sci::tools::recolour_interface(screen);
        sci::vm::ShownScreen recoloured = screen;
        recoloured.visual = visual;
        const auto other = compose(background, recoloured, GraphicsMode::Hd, n);
        SCI_CHECK(!sci::tools::check_isolation(screen, hd, other, n));
    }
}

SCI_TEST(a_wrong_sub_pixel_in_a_window_block_is_found) {
    const CompositionFixture fixture(1);
    const Shown shown(fixture);
    const sci::vm::ShownScreen screen = shown.screen(fixture);
    const sci::render::RgbaImage background = hd_background(scale);
    auto hd = compose(background, screen, GraphicsMode::Hd, scale);
    // The last sub-pixel of the window's first pixel, as an HD background
    // drawn over the window would leave it.
    const int x = CompositionFixture::window.left;
    const int y = CompositionFixture::window.top;
    const std::size_t i = ((static_cast<std::size_t>((y * scale) + scale - 1) * CompositionFixture::width * scale) +
                           static_cast<std::size_t>((x * scale) + scale - 1)) *
                          4;
    hd[i] = static_cast<std::uint8_t>(hd[i] ^ 0x80U);
    const auto problem = sci::tools::check_interface_blocks(screen, hd, scale, true);
    SCI_CHECK(problem && problem->x == x && problem->y == y);
    // Outside the interface, HD mode may differ from the 1x colour; original
    // mode may not.
    const auto original = compose(background, screen, GraphicsMode::Original, scale);
    SCI_CHECK(!sci::tools::check_interface_blocks(screen, original, scale, false));
    SCI_CHECK(
        sci::tools::check_interface_blocks(screen, compose(background, screen, GraphicsMode::Hd, scale), scale, false)
            .has_value());
}

SCI_TEST(a_block_outside_the_window_that_follows_it_is_found) {
    const CompositionFixture fixture(1);
    const Shown shown(fixture);
    const sci::vm::ShownScreen screen = shown.screen(fixture);
    const sci::render::RgbaImage background = hd_background(scale);
    const auto hd = compose(background, screen, GraphicsMode::Hd, scale);
    const std::vector<std::uint8_t> visual = sci::tools::recolour_interface(screen);
    sci::vm::ShownScreen recoloured = screen;
    recoloured.visual = visual;
    auto other = compose(background, recoloured, GraphicsMode::Hd, scale);
    // A pixel right of the window bleeds: it changed with the window.
    const int x = CompositionFixture::window.right;
    const int y = CompositionFixture::window.top + 5;
    const std::size_t i = ((static_cast<std::size_t>(y * scale) * CompositionFixture::width * scale) +
                           static_cast<std::size_t>(x * scale)) *
                          4;
    other[i + 1] = static_cast<std::uint8_t>(other[i + 1] ^ 0x01U);
    const auto problem = sci::tools::check_isolation(screen, hd, other, scale);
    SCI_CHECK(problem && problem->x == x && problem->y == y);
}

SCI_TEST(a_menu_bar_row_the_interface_does_not_own_is_found) {
    const CompositionFixture fixture(1);
    Shown shown(fixture);
    shown.owner[CompositionFixture::at(100, 9)] = static_cast<std::uint8_t>(sci::render::Owner::Picture);
    const auto problem = sci::tools::check_menu_bar(shown.screen(fixture));
    SCI_CHECK(problem && problem->x == 100 && problem->y == 9);
}
