// SPDX-License-Identifier: GPL-3.0-or-later

#include "interface_check.hpp"

#include "sci/render/colors.hpp"

#include <cstddef>
#include <format>

namespace sci::tools {

namespace {

constexpr int width = 320;
constexpr int height = 200;
constexpr int menu_bar_rows = 10;
constexpr auto interface = static_cast<std::uint8_t>(vm::Owner::Interface);

std::size_t at(int x, int y) {
    return (static_cast<std::size_t>(y) * width) + static_cast<std::size_t>(x);
}

// Whether the N x N block of the 1x pixel (x, y) is `colour` throughout.
bool block_is(std::span<const std::uint8_t> frame, int scale, int x, int y, render::Rgb8 colour) {
    const std::size_t row = static_cast<std::size_t>(width) * static_cast<std::size_t>(scale);
    for (int dy = 0; dy < scale; ++dy) {
        for (int dx = 0; dx < scale; ++dx) {
            const std::size_t i =
                ((static_cast<std::size_t>((y * scale) + dy) * row) + static_cast<std::size_t>((x * scale) + dx)) * 4;
            if (frame[i] != colour.r || frame[i + 1] != colour.g || frame[i + 2] != colour.b || frame[i + 3] != 0xFF) {
                return false;
            }
        }
    }
    return true;
}

// Whether the N x N blocks of the 1x pixel (x, y) are equal in both frames.
bool same_block(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b, int scale, int x, int y) {
    const std::size_t row = static_cast<std::size_t>(width) * static_cast<std::size_t>(scale) * 4;
    for (int dy = 0; dy < scale; ++dy) {
        const std::size_t start =
            (static_cast<std::size_t>((y * scale) + dy) * row) + (static_cast<std::size_t>(x * scale) * 4);
        const std::size_t length = static_cast<std::size_t>(scale) * 4;
        for (std::size_t i = start; i < start + length; ++i) {
            if (a[i] != b[i]) {
                return false;
            }
        }
    }
    return true;
}

bool frame_fits(std::span<const std::uint8_t> frame, int scale) {
    return scale >= 1 && frame.size() == static_cast<std::size_t>(width) * height * static_cast<std::size_t>(scale) *
                                             static_cast<std::size_t>(scale) * 4;
}

bool shown_fits(const vm::ShownScreen& shown) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    return shown.visual.size() == pixels && shown.owner.size() == pixels && shown.sprite_layer.size() == pixels;
}

} // namespace

std::optional<FrameProblem> check_interface_blocks(const vm::ShownScreen& shown, std::span<const std::uint8_t> frame,
                                                   int scale, bool hd) {
    if (!shown_fits(shown) || !frame_fits(frame, scale)) {
        return FrameProblem{.x = 0, .y = 0, .what = "the frame or the shown screen has the wrong size"};
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (hd && shown.owner[at(x, y)] != interface) {
                continue;
            }
            const std::uint8_t colour = shown.visual[at(x, y)] & 0x0FU;
            if (!block_is(frame, scale, x, y, render::ega_palette.at(colour))) {
                return FrameProblem{
                    .x = x,
                    .y = y,
                    .what = std::format("the block is not the shown colour {} ({})", colour,
                                        hd && shown.owner[at(x, y)] == interface ? "interface" : "original mode"),
                };
            }
        }
    }
    return std::nullopt;
}

std::optional<FrameProblem> check_menu_bar(const vm::ShownScreen& shown) {
    if (!shown_fits(shown)) {
        return FrameProblem{.x = 0, .y = 0, .what = "the shown screen has the wrong size"};
    }
    for (int y = 0; y < menu_bar_rows; ++y) {
        for (int x = 0; x < width; ++x) {
            if (shown.owner[at(x, y)] != interface) {
                return FrameProblem{.x = x, .y = y, .what = "a menu bar pixel is not interface"};
            }
        }
    }
    return std::nullopt;
}

std::vector<std::uint8_t> recolour_interface(const vm::ShownScreen& shown) {
    std::vector<std::uint8_t> visual(shown.visual.begin(), shown.visual.end());
    for (std::size_t i = 0; i < visual.size() && i < shown.owner.size(); ++i) {
        if (shown.owner[i] == interface) {
            visual[i] = static_cast<std::uint8_t>((visual[i] & 0x0FU) ^ 0x0FU);
        }
    }
    return visual;
}

std::optional<FrameProblem> check_isolation(const vm::ShownScreen& shown, std::span<const std::uint8_t> frame,
                                            std::span<const std::uint8_t> recoloured, int scale) {
    if (!shown_fits(shown) || !frame_fits(frame, scale) || !frame_fits(recoloured, scale)) {
        return FrameProblem{.x = 0, .y = 0, .what = "a frame or the shown screen has the wrong size"};
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (shown.owner[at(x, y)] == interface) {
                const auto colour = static_cast<std::uint8_t>((shown.visual[at(x, y)] & 0x0FU) ^ 0x0FU);
                if (!block_is(recoloured, scale, x, y, render::ega_palette.at(colour))) {
                    return FrameProblem{.x = x, .y = y, .what = "a recoloured interface block is not its new colour"};
                }
            } else if (!same_block(frame, recoloured, scale, x, y)) {
                return FrameProblem{.x = x, .y = y, .what = "a block outside the interface changed with the interface"};
            }
        }
    }
    return std::nullopt;
}

bool window_over_sprite(const vm::ShownScreen& shown) {
    if (!shown_fits(shown)) {
        return false;
    }
    for (int y = menu_bar_rows; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (shown.owner[at(x, y)] != interface) {
                continue;
            }
            for (const vm::SpriteList& list : shown.sprite_lists) {
                for (const vm::ShownSprite& sprite : list.sprites) {
                    const int cx = x - sprite.left;
                    const int cy = y - sprite.top;
                    if (cx >= 0 && cy >= 0 && cx < sprite.width && cy < sprite.height &&
                        sprite.pixels[(static_cast<std::size_t>(cy) * static_cast<std::size_t>(sprite.width)) +
                                      static_cast<std::size_t>(cx)] != sprite.clear_key) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

} // namespace sci::tools
