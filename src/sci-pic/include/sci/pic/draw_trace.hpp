// SPDX-License-Identifier: GPL-3.0-or-later
//
// What path 1 drew, for the vector path (spec section 6.3): every drawing
// primitive with the state it was drawn with, and for every pixel of the
// visual and priority maps the primitive that wrote it last. Recording a
// trace does not change what path 1 draws.
#pragma once

#include "sci/pic/pic_command.hpp"
#include "sci/pic/picture_maps.hpp"

#include <cstdint>
#include <variant>
#include <vector>

namespace sci::pic {

inline constexpr std::uint8_t plane_visual = 1;
inline constexpr std::uint8_t plane_priority = 2;
inline constexpr std::uint8_t plane_control = 4;

// The planes a primitive draws into and its value in each. Values of
// planes not in `planes` are meaningless.
struct DrawState {
    std::uint8_t planes = 0;
    ColorPair pair = 0;        // The EGA pair drawn into the visual map.
    std::uint8_t priority = 0; // 0-15
    std::uint8_t control = 0;  // 0-15
};

// A primitive in picture coordinates (y = 0 is the top of the picture port,
// screen row picture_top), after mirroring. Lines keep their unclipped end
// points; path 1 clips them to the screen when it draws them.
struct TracedPrimitive {
    std::uint16_t picture = 0; // Which draw_picture call on this trace (0 = the first).
    std::uint32_t command = 0; // Index of the command in that picture's stream.
    DrawState state;
    std::variant<Line, Fill, Pattern> shape;
};

inline constexpr std::int32_t no_owner = -1;

struct DrawTrace {
    std::vector<TracedPrimitive> primitives;
    std::uint16_t pictures = 0; // Number of draw_picture calls recorded.
    // Per screen pixel (PictureMaps::index): the index in `primitives` of the
    // primitive that last wrote the visual or priority map, or no_owner
    // where the picture port was cleared and nothing was drawn since.
    std::vector<std::int32_t> visual_owner = std::vector<std::int32_t>(map_size, no_owner);
    std::vector<std::int32_t> priority_owner = std::vector<std::int32_t>(map_size, no_owner);
};

} // namespace sci::pic
