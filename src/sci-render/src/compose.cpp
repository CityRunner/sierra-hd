// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/render/compose.hpp"

#include "sci/core/assert.hpp"
#include "sci/render/colors.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace sci::render {

namespace {

constexpr std::size_t screen_index(int x, int y) {
    return (static_cast<std::size_t>(y) * compose_width) + static_cast<std::size_t>(x);
}

// Where one 1x pixel's block lies in the frame, and the HD maps.
class Block {
public:
    Block(std::span<std::uint8_t> frame, int scale, int x, int y)
        : frame_(frame), scale_(scale),
          frame_width_(static_cast<std::size_t>(compose_width) * static_cast<std::size_t>(scale)),
          left_(static_cast<std::size_t>(x) * static_cast<std::size_t>(scale)),
          top_(static_cast<std::size_t>(y) * static_cast<std::size_t>(scale)) {}

    // The frame offset of sub-pixel (dx, dy).
    [[nodiscard]] std::size_t at(int dx, int dy) const {
        return (((top_ + static_cast<std::size_t>(dy)) * frame_width_) + left_ + static_cast<std::size_t>(dx)) * 4;
    }

    void fill(Rgb8 color) const {
        for (int dy = 0; dy < scale_; ++dy) {
            std::size_t i = at(0, dy);
            for (int dx = 0; dx < scale_; ++dx, i += 4) {
                put(i, color);
            }
        }
    }

    // Copies the block from an image of the picture area (320N x 190N).
    void copy_picture(const RgbaImage& image) const {
        const std::size_t row_bytes = static_cast<std::size_t>(scale_) * 4;
        const std::size_t picture_top =
            static_cast<std::size_t>(compose_picture_top) * static_cast<std::size_t>(scale_);
        for (int dy = 0; dy < scale_; ++dy) {
            const std::size_t from =
                (((top_ - picture_top + static_cast<std::size_t>(dy)) * static_cast<std::size_t>(image.width)) +
                 left_) *
                4;
            std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(from), row_bytes,
                        frame_.begin() + static_cast<std::ptrdiff_t>(at(0, dy)));
        }
    }

    // The block of a map of the picture area (320N x 190N), row by row.
    void read_picture_map(std::span<const std::uint8_t> map, std::span<std::uint8_t> block) const {
        const std::size_t picture_top =
            static_cast<std::size_t>(compose_picture_top) * static_cast<std::size_t>(scale_);
        const auto size = static_cast<std::size_t>(scale_);
        for (std::size_t dy = 0; dy < size; ++dy) {
            const std::size_t from = ((top_ - picture_top + dy) * frame_width_) + left_;
            std::copy_n(map.begin() + static_cast<std::ptrdiff_t>(from), size,
                        block.begin() + static_cast<std::ptrdiff_t>(dy * size));
        }
    }

    void put(std::size_t i, Rgb8 color) const {
        frame_[i] = color.r;
        frame_[i + 1] = color.g;
        frame_[i + 2] = color.b;
        frame_[i + 3] = 0xFF;
    }

private:
    std::span<std::uint8_t> frame_;
    int scale_;
    std::size_t frame_width_;
    std::size_t left_;
    std::size_t top_;
};

Rgb8 ega(std::uint8_t color) {
    return ega_palette[color & 0x0FU];
}

// The sprites of one list that reach into the rectangle composed.
struct Candidates {
    std::uint32_t generation = 0;
    std::vector<const ComposeSprite*> sprites;
};

// The cel pixel a sprite draws at (x, y), if it draws one there.
const std::uint8_t* sprite_pixel(const ComposeSprite& sprite, int x, int y) {
    if (x < sprite.left || x < sprite.clip_left || x >= sprite.left + sprite.width || x >= sprite.clip_right ||
        y < sprite.top || y < sprite.clip_top || y >= sprite.top + sprite.height || y >= sprite.clip_bottom) {
        return nullptr;
    }
    const std::size_t at = (static_cast<std::size_t>(y - sprite.top) * static_cast<std::size_t>(sprite.width)) +
                           static_cast<std::size_t>(x - sprite.left);
    if (at >= sprite.pixels.size() || sprite.pixels[at] == sprite.clear_key) {
        return nullptr;
    }
    return &sprite.pixels[at];
}

bool reaches(const ComposeSprite& sprite, const PixelRect& rect) {
    const int left = std::max({sprite.left, sprite.clip_left, rect.x});
    const int right = std::min({sprite.left + sprite.width, sprite.clip_right, rect.x + rect.width});
    const int top = std::max({sprite.top, sprite.clip_top, rect.y});
    const int bottom = std::min({sprite.top + sprite.height, sprite.clip_bottom, rect.y + rect.height});
    return left < right && top < bottom;
}

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): the three steps of the composition in one loop.
void compose(const ComposeInput& input, int scale, const PixelRect& rect, std::span<std::uint8_t> frame) {
    SCI_ASSERT(scale >= 1 && scale <= max_compose_scale);
    const std::size_t pixels = static_cast<std::size_t>(compose_width) * compose_height;
    SCI_ASSERT(frame.size() == pixels * static_cast<std::size_t>(scale) * static_cast<std::size_t>(scale) * 4);
    SCI_ASSERT(input.visual.size() == pixels && input.owner.size() == pixels);
    const int left = std::max(rect.x, 0);
    const int top = std::max(rect.y, 0);
    const int right = std::min(rect.x + rect.width, compose_width);
    const int bottom = std::min(rect.y + rect.height, compose_height);
    if (left >= right || top >= bottom) {
        return;
    }
    if (!input.hd) {
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                Block(frame, scale, x, y).fill(ega(input.visual[screen_index(x, y)]));
            }
        }
        return;
    }
    SCI_ASSERT(input.background_visual.size() == pixels && input.background_owner.size() == pixels &&
               input.background_priority.size() == pixels && input.background_source.size() == pixels &&
               input.sprite_layer.size() == pixels);
    const std::size_t picture_size = static_cast<std::size_t>(compose_width) * static_cast<std::size_t>(scale) *
                                     static_cast<std::size_t>(compose_height - compose_picture_top) *
                                     static_cast<std::size_t>(scale);
    const RgbaImage* background = input.hd_background;
    if (background != nullptr &&
        (background->width != compose_width * scale || background->pixels.size() != picture_size * 4)) {
        background = nullptr;
    }
    const bool hd_priority = !input.snap && input.hd_priority.size() == picture_size;

    std::vector<Candidates> candidates;
    for (const ComposeSpriteList& list : input.sprite_lists) {
        Candidates found{.generation = list.generation, .sprites = {}};
        for (const ComposeSprite& sprite : list.sprites) {
            if (reaches(sprite, PixelRect{.x = left, .y = top, .width = right - left, .height = bottom - top})) {
                found.sprites.push_back(&sprite);
            }
        }
        if (!found.sprites.empty()) {
            candidates.push_back(std::move(found));
        }
    }

    const std::size_t block_size = static_cast<std::size_t>(scale) * static_cast<std::size_t>(scale);
    std::array<std::uint8_t, static_cast<std::size_t>(max_compose_scale) * max_compose_scale> priority_storage{};
    const std::span<std::uint8_t> priority = std::span(priority_storage).first(block_size);
    const Candidates* last = nullptr;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::size_t i = screen_index(x, y);
            const Block block(frame, scale, x, y);
            // 1. The interface on top.
            if (static_cast<Owner>(input.owner[i]) == Owner::Interface) {
                block.fill(ega(input.visual[i]));
                continue;
            }
            // 2. The background layer.
            const auto background_owner = static_cast<Owner>(input.background_owner[i]);
            const bool in_picture = y >= compose_picture_top;
            if (background != nullptr && in_picture &&
                (background_owner == Owner::Picture || background_owner == Owner::Line)) {
                block.copy_picture(*background);
            } else {
                block.fill(ega(input.background_visual[i]));
            }
            // 3. The sprites.
            const std::uint32_t generation = input.sprite_layer[i];
            if (generation == 0) {
                continue;
            }
            if (last == nullptr || last->generation != generation) {
                const auto found = std::ranges::find(candidates, generation, &Candidates::generation);
                last = found == candidates.end() ? nullptr : &*found;
                if (last == nullptr) {
                    continue;
                }
            }
            bool priority_ready = false;
            for (const ComposeSprite* sprite : last->sprites) {
                const std::uint8_t* pixel = sprite_pixel(*sprite, x, y);
                if (pixel == nullptr) {
                    continue;
                }
                if (!priority_ready) {
                    if (hd_priority && in_picture && input.background_source[i] == priority_source_picture) {
                        block.read_picture_map(input.hd_priority, priority);
                    } else {
                        std::ranges::fill(priority, input.background_priority[i]);
                    }
                    priority_ready = true;
                }
                // As GfxView::draw: compared as a byte, written up to 15.
                const auto sprite_priority = static_cast<std::uint8_t>(sprite->priority);
                const Rgb8 color = ega(*pixel);
                std::size_t k = 0;
                for (int dy = 0; dy < scale; ++dy) {
                    std::size_t at = block.at(0, dy);
                    for (int dx = 0; dx < scale; ++dx, ++k, at += 4) {
                        if (sprite_priority >= priority[k]) {
                            block.put(at, color);
                            if (sprite_priority <= 15) {
                                priority[k] = sprite_priority;
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace sci::render
