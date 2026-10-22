// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/game_frame.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

namespace sci::platform {

namespace {

// The interpreter's owner values are the compositor's.
static_assert(static_cast<int>(vm::Owner::Picture) == static_cast<int>(render::Owner::Picture));
static_assert(static_cast<int>(vm::Owner::AddToPic) == static_cast<int>(render::Owner::AddToPic));
static_assert(static_cast<int>(vm::Owner::Sprite) == static_cast<int>(render::Owner::Sprite));
static_assert(static_cast<int>(vm::Owner::Primitive) == static_cast<int>(render::Owner::Primitive));
static_assert(static_cast<int>(vm::Owner::Line) == static_cast<int>(render::Owner::Line));
static_assert(static_cast<int>(vm::Owner::Interface) == static_cast<int>(render::Owner::Interface));
static_assert(vm::priority_from_picture == render::priority_source_picture);

constexpr int width = render::compose_width;
constexpr int height = render::compose_height;
constexpr std::size_t pixel_count = static_cast<std::size_t>(width) * height;
// Changes are composed in tiles of 16 x 16 pixels, merged along tile rows.
constexpr int tile = 16;
constexpr int tile_columns = (width + tile - 1) / tile;
constexpr int tile_rows = (height + tile - 1) / tile;

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): change detection and tiling in one pass.
void GameFrame::update(const vm::ShownScreen& shown, GraphicsMode mode, int scale, bool snap) {
    if (shown.visual.size() != pixel_count || shown.owner.size() != pixel_count ||
        shown.background_visual.size() != pixel_count || shown.sprite_layer.size() != pixel_count) {
        return; // Nothing shown yet.
    }
    const bool hd = mode == GraphicsMode::Hd;
    HdImages images;
    if (hd) {
        images = source_(shown.pictures, shown.lines, scale);
    }
    const bool picture_changed =
        !std::ranges::equal(shown.pictures, pictures_) || !std::ranges::equal(shown.lines, lines_);
    const bool full =
        scale != scale_ || mode != mode_ || snap != snap_ || visual_.size() != pixel_count || (hd && picture_changed);
    if (scale != scale_) {
        scale_ = scale;
        frame_.assign(pixel_count * static_cast<std::size_t>(scale) * static_cast<std::size_t>(scale) * 4, 0);
    }
    mode_ = mode;
    snap_ = snap;

    std::array<bool, static_cast<std::size_t>(tile_columns) * tile_rows> dirty{};
    if (full) {
        dirty.fill(true);
    } else {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * width) + static_cast<std::size_t>(x);
                const bool changed =
                    shown.visual[i] != visual_[i] ||
                    (hd && (shown.owner[i] != owner_[i] || shown.background_visual[i] != background_visual_[i] ||
                            shown.background_owner[i] != background_owner_[i] ||
                            shown.background_priority[i] != background_priority_[i] ||
                            shown.background_source[i] != background_source_[i] ||
                            shown.sprite_layer[i] != sprite_layer_[i]));
                if (changed) {
                    dirty[(static_cast<std::size_t>(y / tile) * tile_columns) + static_cast<std::size_t>(x / tile)] =
                        true;
                }
            }
        }
    }

    // The sprite lists as the compositor takes them.
    std::vector<std::vector<render::ComposeSprite>> sprites;
    sprites.reserve(shown.sprite_lists.size());
    std::vector<render::ComposeSpriteList> lists;
    for (const vm::SpriteList& list : shown.sprite_lists) {
        std::vector<render::ComposeSprite>& converted = sprites.emplace_back();
        for (const vm::ShownSprite& sprite : list.sprites) {
            converted.push_back(render::ComposeSprite{
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
        lists.push_back(render::ComposeSpriteList{.generation = list.generation, .sprites = converted});
    }
    const render::ComposeInput input{
        .visual = shown.visual,
        .owner = shown.owner,
        .background_visual = shown.background_visual,
        .background_owner = shown.background_owner,
        .background_priority = shown.background_priority,
        .background_source = shown.background_source,
        .sprite_layer = shown.sprite_layer,
        .sprite_lists = lists,
        .hd_background = images.background,
        .hd_priority = images.priority,
        .hd = hd,
        .snap = snap,
    };

    last_composed_ = 0;
    for (int row = 0; row < tile_rows; ++row) {
        for (int column = 0; column < tile_columns;) {
            if (!dirty[(static_cast<std::size_t>(row) * tile_columns) + static_cast<std::size_t>(column)]) {
                ++column;
                continue;
            }
            const int first = column;
            while (column < tile_columns &&
                   dirty[(static_cast<std::size_t>(row) * tile_columns) + static_cast<std::size_t>(column)]) {
                ++column;
            }
            const render::PixelRect rect{
                .x = first * tile,
                .y = row * tile,
                .width = std::min(column * tile, width) - (first * tile),
                .height = std::min((row + 1) * tile, height) - (row * tile),
            };
            render::compose(input, scale_, rect, frame_);
            last_composed_ += static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height);
            dirty_.push_back(render::PixelRect{
                .x = rect.x * scale_,
                .y = rect.y * scale_,
                .width = rect.width * scale_,
                .height = rect.height * scale_,
            });
        }
    }
    remember(shown);
}

void GameFrame::remember(const vm::ShownScreen& shown) {
    visual_.assign(shown.visual.begin(), shown.visual.end());
    owner_.assign(shown.owner.begin(), shown.owner.end());
    background_visual_.assign(shown.background_visual.begin(), shown.background_visual.end());
    background_owner_.assign(shown.background_owner.begin(), shown.background_owner.end());
    background_priority_.assign(shown.background_priority.begin(), shown.background_priority.end());
    background_source_.assign(shown.background_source.begin(), shown.background_source.end());
    sprite_layer_.assign(shown.sprite_layer.begin(), shown.sprite_layer.end());
    pictures_.assign(shown.pictures.begin(), shown.pictures.end());
    lines_.assign(shown.lines.begin(), shown.lines.end());
}

std::vector<render::PixelRect> GameFrame::take_dirty() {
    return std::exchange(dirty_, {});
}

} // namespace sci::platform
