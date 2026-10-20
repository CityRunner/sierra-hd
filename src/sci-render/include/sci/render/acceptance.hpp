// SPDX-License-Identifier: GPL-3.0-or-later
//
// The acceptance check of the HD pipeline (spec section 6.4): the visual
// scene, reduced back to 1x, against path 1's pair map.
//
// - Pair classes: pairs are compared by the colour they blend to, so a pair
//   and its mirror image ((a, b) and (b, a)) are one class. Blended colours
//   are never compared with dithered pixels.
// - Agreement: the share of pixels outside the edge band (pixels with a
//   neighbour of another class, among their eight) whose reduced class is
//   the map's. The target is at least 99%.
// - Swaps: groups of four or more 4-connected differing pixels outside the
//   edge band, which would be a region shown in another colour rather than
//   an edge effect. The target is none.
// - Lines: every pixel a visible PIC line owns at 1x must be touched by the
//   line's stroke (at least one of its 16 x 16 samples lies within the
//   stroke). Pixels whose samples later layers cover completely are counted
//   separately; they show the drawing order, not a missing line.
#pragma once

#include "sci/pic/draw_trace.hpp"
#include "sci/pic/picture_maps.hpp"
#include "sci/vector/scene.hpp"

namespace sci::render {

struct Acceptance {
    int compared_pixels = 0;  // Outside the edge band.
    int differing_pixels = 0; // Of those, with another class after reduction.
    int swaps = 0;
    int line_pixels = 0;            // Pixels owned by visible lines.
    int untouched_line_pixels = 0;  // Not touched by their line's stroke.
    int overlapped_line_pixels = 0; // Touched, but covered by later layers.

    [[nodiscard]] double agreement() const {
        return compared_pixels == 0 ? 1.0 : 1.0 - (static_cast<double>(differing_pixels) / compared_pixels);
    }

    [[nodiscard]] bool passes() const { return agreement() >= 0.99 && swaps == 0 && untouched_line_pixels == 0; }
};

// Checks a visual scene built from these maps and trace.
[[nodiscard]] Acceptance check_acceptance(const vector::Scene& visual, const pic::PictureMaps& maps,
                                          const pic::DrawTrace& trace);

} // namespace sci::render
