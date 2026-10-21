// SPDX-License-Identifier: GPL-3.0-or-later
//
// The checks of spec section 6.10 (addendum A1 in docs/SPEC_ADDENDA.md):
// drawing order, occlusion and stroke integrity in the HD scene, measured
// against what path 1 shows at 1x.
//
// The scene is sampled on a grid of `density` x `density` points per
// original pixel; every point records the layer that shows there. The 1x
// side comes from the trace: which primitive owns each pixel (what the
// player saw) and which pixels each line drew.
//
// - Overshoot: a line end touches a contour where the end pixel is seen,
//   another line passes next to it and is seen there, the end did not draw
//   over the contour, and no end of the contour is next to it. Near the
//   end, no sample may show the ending line inside the contour's stroke or
//   beyond its centreline. (The vector path uses the same definition.)
// - Depth: no sample may show a layer at a pixel that is neither the layer's
//   own at 1x nor in the 1x edge band (pixels with a neighbour of another
//   pair class) when the 1x priority there is nearer than the layer's own.
// - Doubled boundary: two lines that drew the same pixels (two or more in
//   common) are one boundary. On those pixels, HD must not show one line
//   where the player saw the other (a quarter of a pixel's samples or
//   more).
// - Fragment: every 4-connected group of samples showing a line contains a
//   sample in a pixel the line owns at 1x.
// - Break: (a) the samples showing a line over one 8-connected group of the
//   pixels it owns at 1x are connected; (b) where the end pixel of a line
//   touches a pixel of another line at 1x (a joint; the agreed reading of
//   "connected" in A1), the two lines' samples meet near that pixel.
//
// The checks only read the scene; they never change it.
#pragma once

#include "sci/core/executor.hpp"
#include "sci/pic/draw_trace.hpp"
#include "sci/pic/picture_maps.hpp"
#include "sci/vector/scene.hpp"

#include <cstdint>
#include <vector>

namespace sci::render {

enum class IntegrityKind : std::int8_t { Overshoot, Depth, Doubled, Fragment, Break };

// One finding: the primitives involved (indices in the trace; `other` is -1
// where only one is) and a picture pixel where it shows.
struct IntegrityCase {
    IntegrityKind kind = IntegrityKind::Overshoot;
    std::int32_t primitive = -1;
    std::int32_t other = -1;
    int x = 0;
    int y = 0;
};

struct Integrity {
    int contacts = 0;     // Line ends on another line's contour (checked for overshoot).
    int shared_pairs = 0; // Pairs of lines that drew the same pixels.
    int joints = 0;       // Joints between lines at 1x.
    int line_groups = 0;  // 8-connected groups of owned line pixels.
    int overshoots = 0;
    int depth_violations = 0; // Pixels.
    int doubled = 0;
    int fragments = 0;
    int breaks = 0;        // The three below.
    int split_groups = 0;  // Owned line groups shown in pieces.
    int hidden_groups = 0; // Owned line groups not shown at all.
    int broken_joints = 0;
    std::vector<IntegrityCase> cases;

    [[nodiscard]] bool passes() const {
        return overshoots == 0 && depth_violations == 0 && doubled == 0 && fragments == 0 && breaks == 0;
    }
};

inline constexpr int integrity_density = 8;

// Checks a visual scene built from these maps and trace.
[[nodiscard]] Integrity check_integrity(const vector::Scene& visual, const pic::PictureMaps& maps,
                                        const pic::DrawTrace& trace, core::Executor& executor,
                                        int density = integrity_density);

} // namespace sci::render
