// SPDX-License-Identifier: GPL-3.0-or-later
//
// A synthetic game screen for the composition tests (sci/render/compose.hpp)
// and the golden hash of a composed frame: a picture split by a diagonal
// into a background face (priority 4) and a foreground face (priority 12);
// a sprite of priority 8 partly behind the foreground face and partly under
// a window; an addToPic block of priority 13 with a second sprite partly
// behind it. The shown screen is what the interpreter would have shown:
// the sprites drawn at 1x by GfxView::draw's rule.

#pragma once

#include "pic_builder.hpp"
#include "sci/core/executor.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/render/compose.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vector/build_scene.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sci::test {

struct FixtureRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    [[nodiscard]] bool contains(int x, int y) const { return x >= left && x < right && y >= top && y < bottom; }
};

class CompositionFixture {
public:
    static constexpr int width = render::compose_width;
    static constexpr int height = render::compose_height;
    static constexpr std::size_t pixels = static_cast<std::size_t>(width) * height;
    static constexpr std::uint8_t sprite_color = 14; // In no picture.
    static constexpr std::uint8_t second_sprite_color = 10;
    static constexpr std::uint8_t window_color = 7;
    static constexpr std::uint8_t add_to_pic_color = 13;
    static constexpr std::uint8_t add_to_pic_priority = 13;
    static constexpr int sprite_priority = 8;
    // Screen coordinates.
    static constexpr FixtureRect sprite{.left = 140, .top = 50, .right = 180, .bottom = 110};
    static constexpr FixtureRect second_sprite{.left = 70, .top = 130, .right = 90, .bottom = 140};
    static constexpr FixtureRect window{.left = 120, .top = 40, .right = 160, .bottom = 70};
    static constexpr FixtureRect add_to_pic{.left = 60, .top = 120, .right = 80, .bottom = 150};

    // The sprites refer to the fixture's own cels.
    CompositionFixture(const CompositionFixture&) = delete;
    CompositionFixture& operator=(const CompositionFixture&) = delete;
    CompositionFixture(CompositionFixture&&) = delete;
    CompositionFixture& operator=(CompositionFixture&&) = delete;
    ~CompositionFixture() = default;

    explicit CompositionFixture(int scale) : scale_(scale) {
        PicBuilder pic;
        pic.color(0).priority(4).rectangle(10, 10, 300, 180);
        pic.line(120, 10, 220, 180); // The diagonal.
        pic.color(1).priority(4).fill(50, 100);
        pic.color(6).priority(12).fill(280, 100); // The foreground face.
        const auto commands = pic::parse_pic(pic.end());
        pic::DrawTrace trace;
        const pic::DrawOptions options{.mirrored = false, .add_to = false, .ega_palette = 0};
        drawn_ = commands.has_value() && pic::draw_picture(*commands, maps_, options, &trace).has_value();
        core::SerialExecutor executor;
        background_ =
            render::render_hd(vector::build_scene(maps_, trace, vector::Plane::Visual).scene, scale, executor);
        hd_priority_ =
            render::render_priority(vector::build_scene(maps_, trace, vector::Plane::Priority).scene, scale, executor);

        // The background layer: the picture, the addToPic block, the window.
        visual_.assign(maps_.visual.begin(), maps_.visual.end());
        owner_.assign(pixels, static_cast<std::uint8_t>(render::Owner::Picture));
        priority_.assign(maps_.priority.begin(), maps_.priority.end());
        source_.assign(pixels, render::priority_source_picture);
        for (int y = 0; y < 10; ++y) {
            for (int x = 0; x < width; ++x) {
                owner_[at(x, y)] = static_cast<std::uint8_t>(render::Owner::Interface);
            }
        }
        fill(add_to_pic, add_to_pic_color, render::Owner::AddToPic);
        for (int y = add_to_pic.top; y < add_to_pic.bottom; ++y) {
            for (int x = add_to_pic.left; x < add_to_pic.right; ++x) {
                priority_[at(x, y)] = add_to_pic_priority;
                source_[at(x, y)] = 0;
            }
        }
        background_visual_ = visual_;
        background_owner_ = owner_;

        // The sprites, drawn at 1x as GfxView::draw does. The cels have a
        // transparent first column.
        cel_ = make_cel(sprite, sprite_color);
        second_cel_ = make_cel(second_sprite, second_sprite_color);
        sprites_ = {
            make_sprite(sprite, cel_),
            make_sprite(second_sprite, second_cel_),
        };
        layer_.assign(pixels, 0);
        std::vector<std::uint8_t> priority_1x = priority_;
        for (const render::ComposeSprite& s : sprites_) {
            for (int y = s.top; y < s.top + s.height; ++y) {
                for (int x = s.left; x < s.left + s.width; ++x) {
                    layer_[at(x, y)] = 1;
                    const std::uint8_t color = s.pixels[static_cast<std::size_t>(((y - s.top) * s.width) + x - s.left)];
                    if (color != s.clear_key && sprite_priority >= priority_1x[at(x, y)]) {
                        visual_[at(x, y)] = color;
                        owner_[at(x, y)] = static_cast<std::uint8_t>(render::Owner::Sprite);
                        priority_1x[at(x, y)] = sprite_priority;
                    }
                }
            }
        }
        // The window over everything, shown with the sprites.
        fill(window, window_color, render::Owner::Interface);
        lists_ = {render::ComposeSpriteList{.generation = 1, .sprites = sprites_}};
    }

    [[nodiscard]] render::ComposeInput input(bool snap) const {
        return render::ComposeInput{
            .visual = visual_,
            .owner = owner_,
            .background_visual = background_visual_,
            .background_owner = background_owner_,
            .background_priority = priority_,
            .background_source = source_,
            .sprite_layer = layer_,
            .sprite_lists = lists_,
            .hd_background = &background_,
            .hd_priority = hd_priority_,
            .hd = true,
            .snap = snap,
        };
    }

    [[nodiscard]] std::vector<std::uint8_t> compose(bool snap) const {
        std::vector<std::uint8_t> frame(pixels * static_cast<std::size_t>(scale_) * static_cast<std::size_t>(scale_) *
                                        4);
        render::compose(input(snap), scale_, {.x = 0, .y = 0, .width = width, .height = height}, frame);
        return frame;
    }

    [[nodiscard]] static std::size_t at(int x, int y) {
        return (static_cast<std::size_t>(y) * width) + static_cast<std::size_t>(x);
    }

    [[nodiscard]] const std::vector<std::uint8_t>& visual() const { return visual_; }

    [[nodiscard]] const std::vector<std::uint8_t>& owner() const { return owner_; }

    [[nodiscard]] const std::vector<std::uint8_t>& background_visual() const { return background_visual_; }

    [[nodiscard]] const std::vector<std::uint8_t>& background_owner() const { return background_owner_; }

    [[nodiscard]] const std::vector<std::uint8_t>& background_priority() const { return priority_; }

    [[nodiscard]] const std::vector<std::uint8_t>& background_source() const { return source_; }

    [[nodiscard]] const std::vector<std::uint32_t>& sprite_layer() const { return layer_; }

    [[nodiscard]] const std::vector<render::ComposeSprite>& sprites() const { return sprites_; }

    // Whether the synthetic picture was drawn.
    [[nodiscard]] bool drawn() const { return drawn_; }

    [[nodiscard]] const pic::PictureMaps& maps() const { return maps_; }

    [[nodiscard]] const render::RgbaImage& background() const { return background_; }

    [[nodiscard]] const std::vector<std::uint8_t>& hd_priority() const { return hd_priority_; }

private:
    void fill(const FixtureRect& rect, std::uint8_t color, render::Owner owner) {
        for (int y = rect.top; y < rect.bottom; ++y) {
            for (int x = rect.left; x < rect.right; ++x) {
                visual_[at(x, y)] = color;
                owner_[at(x, y)] = static_cast<std::uint8_t>(owner);
            }
        }
    }

    static std::vector<std::uint8_t> make_cel(const FixtureRect& rect, std::uint8_t color) {
        const int w = rect.right - rect.left;
        const int h = rect.bottom - rect.top;
        std::vector<std::uint8_t> cel(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), color);
        for (int y = 0; y < h; ++y) {
            cel[static_cast<std::size_t>(y) * static_cast<std::size_t>(w)] = 15; // The clear key.
        }
        return cel;
    }

    static render::ComposeSprite make_sprite(const FixtureRect& rect, const std::vector<std::uint8_t>& cel) {
        return render::ComposeSprite{
            .left = rect.left,
            .top = rect.top,
            .width = rect.right - rect.left,
            .height = rect.bottom - rect.top,
            .pixels = cel,
            .clear_key = 15,
            .priority = sprite_priority,
            .clip_left = 0,
            .clip_top = 10,
            .clip_right = width,
            .clip_bottom = height,
        };
    }

    int scale_;
    bool drawn_ = false;
    pic::PictureMaps maps_;
    render::RgbaImage background_;
    std::vector<std::uint8_t> hd_priority_;
    std::vector<std::uint8_t> visual_;
    std::vector<std::uint8_t> owner_;
    std::vector<std::uint8_t> priority_;
    std::vector<std::uint8_t> source_;
    std::vector<std::uint8_t> background_visual_;
    std::vector<std::uint8_t> background_owner_;
    std::vector<std::uint32_t> layer_;
    std::vector<std::uint8_t> cel_;
    std::vector<std::uint8_t> second_cel_;
    std::vector<render::ComposeSprite> sprites_;
    std::vector<render::ComposeSpriteList> lists_;
};

} // namespace sci::test
