// SPDX-License-Identifier: GPL-3.0-or-later
//
// The composition of the game screen at N (spec sections 6.6, 6.8 and 6.9;
// docs/hd-composition.md). Every 1x pixel of the screen becomes an N x N
// block of the frame, and depends only on that pixel's data:
//   1. a pixel the interface owns is the block of its EGA colour;
//   2. otherwise the background layer: the HD background where a picture
//      (or a kernel line) drew it, else the block of its EGA colour;
//   3. then the sprites shown with the pixel, in drawing order: each HD
//      sub-pixel of an opaque cel pixel takes the cel's colour where the
//      sprite's priority is at least the HD priority there, which comes
//      from the vector priority map where the picture set the priority
//      (from the 1x priority elsewhere, or always with snapping), and the
//      sprite writes its priority, as at 1x.
// Original mode draws the block of the shown EGA colour. The composition
// uses integers only, so frames are the same on every machine.
#pragma once

#include "sci/render/rasterize.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sci::render {

inline constexpr int compose_width = 320;  // The screen in 1x pixels.
inline constexpr int compose_height = 200; //
inline constexpr int compose_picture_top = 10;

// What drew a visual pixel; the values of sci::vm::Owner.
enum class Owner : std::uint8_t {
    Picture = 0,
    AddToPic = 1,
    Sprite = 2,
    Primitive = 3,
    Line = 4,
    Interface = 5,
};

// The priority source value of a priority the picture set.
inline constexpr std::uint8_t priority_source_picture = 1;

// A cel drawn by kAnimate, in screen coordinates.
struct ComposeSprite {
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::span<const std::uint8_t> pixels; // width x height EGA colours.
    std::uint8_t clear_key = 0;
    int priority = 0; // As the interpreter passed it; compared as a byte.
    int clip_left = 0;
    int clip_top = 0;
    int clip_right = 0;
    int clip_bottom = 0;
};

// The sprites shown with the pixels that name `generation`.
struct ComposeSpriteList {
    std::uint32_t generation = 0;
    std::span<const ComposeSprite> sprites;
};

struct ComposeInput {
    // 320 x 200 each, row by row (sci::vm::ShownScreen).
    std::span<const std::uint8_t> visual;
    std::span<const std::uint8_t> owner;
    std::span<const std::uint8_t> background_visual;
    std::span<const std::uint8_t> background_owner;
    std::span<const std::uint8_t> background_priority;
    std::span<const std::uint8_t> background_source;
    std::span<const std::uint32_t> sprite_layer;
    std::span<const ComposeSpriteList> sprite_lists;
    // The picture area at 320N x 190N: the HD background (null: 1x blocks)
    // and the HD priority map (empty: the 1x priorities as blocks).
    const RgbaImage* hd_background = nullptr;
    std::span<const std::uint8_t> hd_priority;
    bool hd = true;    // False: original mode.
    bool snap = false; // The configuration's priority_snap.
};

// A rectangle of 1x pixels.
struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const PixelRect&, const PixelRect&) = default;
};

// The largest N the composition supports.
inline constexpr int max_compose_scale = 16;

// Composes the 1x pixels of `rect` into `frame`, 320N x 200N RGBA pixels.
void compose(const ComposeInput& input, int scale, const PixelRect& rect, std::span<std::uint8_t> frame);

} // namespace sci::render
