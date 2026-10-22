// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's frame (sci/platform/game_frame.hpp) on the synthetic screen of
// tests/unit/render/composition_fixture.hpp: the frame is the composition,
// and an update composes only the tiles that changed.

#include "composition_fixture.hpp"
#include "sci/platform/config.hpp"
#include "sci/platform/game_frame.hpp"
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

constexpr int scale = 2;

// The fixture as the interpreter shows it, with its sprites moved by `dx`.
struct Shown {
    explicit Shown(const CompositionFixture& fixture) {
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
        list.generation = 1;
        lists = {list};
    }

    [[nodiscard]] sci::vm::ShownScreen screen(const CompositionFixture& fixture, std::uint64_t serial) const {
        return sci::vm::ShownScreen{
            .visual = fixture.visual(),
            .serial = serial,
            .owner = fixture.owner(),
            .background_visual = fixture.background_visual(),
            .background_owner = fixture.background_owner(),
            .background_priority = fixture.background_priority(),
            .background_source = fixture.background_source(),
            .sprite_layer = layer.empty() ? std::span<const std::uint32_t>(fixture.sprite_layer()) : layer,
            .sprite_lists = lists,
            .pictures = pictures,
            .lines = {},
            .cursor = -1,
            .mouse = sci::vm::Point{.x = 0, .y = 0},
        };
    }

    sci::vm::SpriteList list;
    std::vector<sci::vm::SpriteList> lists;
    std::vector<std::uint32_t> layer;
    std::vector<sci::vm::ShownPicture> pictures{{.number = 1, .mirrored = false, .ega_palette = 0}};
};

GameFrame frame_of(const CompositionFixture& fixture) {
    return GameFrame([&fixture](std::span<const sci::vm::ShownPicture> /*stack*/,
                                std::span<const sci::vm::ShownLine> /*lines*/, int /*scale*/) {
        return HdImages{.background = &fixture.background(), .priority = fixture.hd_priority()};
    });
}

} // namespace

SCI_TEST(the_frame_is_the_composition_of_the_shown_screen) {
    const CompositionFixture fixture(scale);
    const Shown shown(fixture);
    GameFrame frame = frame_of(fixture);
    for (const bool snap : {false, true}) {
        frame.update(shown.screen(fixture, 1), GraphicsMode::Hd, scale, snap);
        SCI_CHECK(frame.frame() == fixture.compose(snap));
        const auto dirty = frame.take_dirty();
        SCI_CHECK(!dirty.empty());
        SCI_CHECK(frame.last_composed() == CompositionFixture::pixels);
    }
}

SCI_TEST(an_update_composes_only_the_changed_tiles) {
    const CompositionFixture fixture(scale);
    Shown shown(fixture);
    GameFrame frame = frame_of(fixture);
    frame.update(shown.screen(fixture, 1), GraphicsMode::Hd, scale, false);
    (void)frame.take_dirty();
    // Nothing changed: nothing composed.
    frame.update(shown.screen(fixture, 2), GraphicsMode::Hd, scale, false);
    SCI_CHECK(frame.take_dirty().empty());
    SCI_CHECK(frame.last_composed() == 0);
    // The pixels around (100, 100) shown again without sprites.
    shown.layer = fixture.sprite_layer();
    shown.layer[CompositionFixture::at(150, 100)] = 0;
    frame.update(shown.screen(fixture, 3), GraphicsMode::Hd, scale, false);
    const auto dirty = frame.take_dirty();
    SCI_CHECK(dirty.size() == 1);
    if (!dirty.empty()) {
        SCI_CHECK(dirty.front() == (sci::render::PixelRect{
                                       .x = 144 * scale, .y = 96 * scale, .width = 16 * scale, .height = 16 * scale}));
    }
    SCI_CHECK(frame.last_composed() == 256);
    // The same as composing everything.
    sci::render::ComposeInput input = fixture.input(false);
    input.sprite_layer = shown.layer;
    std::vector<std::uint8_t> expected(frame.frame().size());
    sci::render::compose(input, scale, {.x = 0, .y = 0, .width = 320, .height = 200}, expected);
    SCI_CHECK(frame.frame() == expected);
}

SCI_TEST(original_mode_needs_no_hd_images) {
    const CompositionFixture fixture(scale);
    const Shown shown(fixture);
    bool asked = false;
    GameFrame frame([&asked](std::span<const sci::vm::ShownPicture>, std::span<const sci::vm::ShownLine>, int) {
        asked = true;
        return HdImages{};
    });
    frame.update(shown.screen(fixture, 1), GraphicsMode::Original, scale, false);
    SCI_CHECK(!asked);
    sci::render::ComposeInput input = fixture.input(false);
    input.hd = false;
    std::vector<std::uint8_t> expected(frame.frame().size());
    sci::render::compose(input, scale, {.x = 0, .y = 0, .width = 320, .height = 200}, expected);
    SCI_CHECK(frame.frame() == expected);
}
