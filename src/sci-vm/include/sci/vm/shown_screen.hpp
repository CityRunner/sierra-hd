// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the interpreter keeps beside its 1x maps for the HD composition
// (docs/hd-composition.md): who drew each visual pixel, where the priority
// came from, the layer under the sprites, the sprites themselves, and the
// picture stack with the kernel lines drawn into it. The game logic never
// reads any of it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sci::vm {

// What last drew a visual pixel.
enum class Owner : std::uint8_t {
    Picture = 0,   // A picture or a picture overlay (kDrawPic).
    AddToPic = 1,  // A static view drawn into the picture (kAddToPic).
    Sprite = 2,    // A cel drawn by kAnimate.
    Primitive = 3, // A kernel box (kGraph FillBox) into the picture window.
    Line = 4,      // A kernel line (kGraph DrawLine) into the picture window.
    Interface = 5, // Everything else: text, windows, menus, controls, kDrawCel.
};

// The priority source map's values: whether a priority value came from a
// picture or from later drawing.
inline constexpr std::uint8_t priority_from_picture = 1;
inline constexpr std::uint8_t priority_from_drawing = 0;

// One picture of the picture stack: the base picture, then the overlays
// drawn over it (kDrawPic with add-to), in order.
struct ShownPicture {
    int number = -1;
    bool mirrored = false;
    int ega_palette = 0;

    friend bool operator==(const ShownPicture&, const ShownPicture&) = default;
};

// A kernel line drawn into the picture window's visual map, in screen
// coordinates as drawn (clipped), and its EGA colour.
struct ShownLine {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    std::uint8_t color = 0;

    friend bool operator==(const ShownLine&, const ShownLine&) = default;
};

// A cel kAnimate drew, in screen coordinates. `pixels` (width x height
// colours, mirroring applied) stay valid as long as the interpreter does.
struct ShownSprite {
    int view = 0;
    int loop = 0;
    int cel = 0;
    int left = 0;
    int top = 0;
    int width = 0;
    int height = 0;
    std::span<const std::uint8_t> pixels;
    std::uint8_t clear_key = 0;
    int priority = 0; // Above 15: drawn over everything, priority map untouched.
    // The clip rectangle (the picture window), [left, right) x [top, bottom).
    int clip_left = 0;
    int clip_top = 0;
    int clip_right = 0;
    int clip_bottom = 0;

    friend bool operator==(const ShownSprite& a, const ShownSprite& b) {
        return a.view == b.view && a.loop == b.loop && a.cel == b.cel && a.left == b.left && a.top == b.top &&
               a.width == b.width && a.height == b.height && a.pixels.data() == b.pixels.data() &&
               a.pixels.size() == b.pixels.size() && a.clear_key == b.clear_key && a.priority == b.priority &&
               a.clip_left == b.clip_left && a.clip_top == b.clip_top && a.clip_right == b.clip_right &&
               a.clip_bottom == b.clip_bottom;
    }
};

// The sprites of one kAnimate cycle, in drawing order. Each shown pixel
// names the list it was shown with (0 for none), so a pixel shown again
// without the sprites (text shown between two cycles, ScummVM's behaviour)
// loses them, and one not shown again keeps them.
struct SpriteList {
    std::uint32_t generation = 0;
    std::vector<ShownSprite> sprites;
};

} // namespace sci::vm
