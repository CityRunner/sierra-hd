// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stroke integrity and occlusion between lines (spec section 6.10,
// addendum A1 in docs/SPEC_ADDENDA.md), applied to the strokes of the
// visual scene after they are built. The evidence is what path 1 shows at
// 1x: which line owns each pixel (what the player saw) and which pixels
// each line drew.
//
// - Corner joints: two line ends on diagonally adjacent pixels, both seen
//   at 1x, meet at the midpoint of their end points, so the strokes join.
// - Ends on a contour: a line whose end pixel touches another line where
//   that line passes is extended or trimmed to the contour's centreline and
//   cut along the contour's near edge, so it neither overlaps nor leaves a
//   gap. Not where the ending line drew over the contour's pixel (the
//   player saw it there).
// - Shared boundaries: where two lines drew the same pixels and the player
//   saw one of them, the other line's stroke is removed along that run, so
//   the boundary shows once, in the colour seen at 1x.
#pragma once

#include "sci/vector/build_scene.hpp"
#include "sci/vector/scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sci::vector::detail {

struct StrokeLine {
    std::int32_t primitive = -1;
    std::vector<std::size_t> path; // The picture pixels the line drew, from its start (y * width + x).
};

// Changes the strokes of the lines (layers whose source is a line's
// primitive) and records every change in the stats. `owners` holds the 1x
// owner of every picture pixel.
void apply_stroke_integrity(std::vector<Layer>& layers, std::span<const StrokeLine> lines,
                            std::span<const std::int32_t> owners, SceneStats& stats);

} // namespace sci::vector::detail
