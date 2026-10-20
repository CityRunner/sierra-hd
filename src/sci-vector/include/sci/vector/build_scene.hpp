// SPDX-License-Identifier: GPL-3.0-or-later
//
// Turns what path 1 drew into a vector scene (spec section 6.3, conservative
// mode). The faces come from the planar graph of the PIC lines; each face
// takes its value from the path-1 map, and a face whose pixels disagree,
// or that contains no pixel centre, falls back to the 1x blocks (spec
// section 6.4).
#pragma once

#include "sci/pic/draw_trace.hpp"
#include "sci/pic/picture_maps.hpp"
#include "sci/vector/scene.hpp"

namespace sci::vector {

// What the analysis did, for the replacement report.
struct SceneStats {
    int lines = 0;           // PIC lines drawn into the plane.
    int strokes = 0;         // Lines drawn as strokes.
    int hidden_lines = 0;    // Lines left out because nothing of them is visible.
    int extensions = 0;      // Gaps closed by extending a line to the line it touches at 1x.
    int joins = 0;           // Gaps closed by joining two nearby end points.
    int perpendiculars = 0;  // Gaps closed by a connector across to the touching line.
    int faces = 0;           // Faces containing at least one pixel centre.
    int uniform_faces = 0;   // Faces drawn in one value.
    int thin_faces = 0;      // Fallback: no pixel of the face's own region.
    int mixed_faces = 0;     // Fallback: the face's pixels disagree outside the edge band.
    int fallback_pixels = 0; // Pixel centres in fallback faces.
    int brushes = 0;         // Solid brushes drawn as shapes.
    int textured = 0;        // Textured brushes drawn as blocks.
    // Chains of short lines replaced by fitted curves.
    int straightened = 0;  // By one straight line.
    int arcs = 0;          // By circular arcs.
    int splines = 0;       // By cubic splines.
    int ellipses = 0;      // Closed chains, by ellipses.
    int fitted_lines = 0;  // PIC lines replaced by these curves.
    int rejected_fits = 0; // Fits left out because they would change the faces.
};

struct BuiltScene {
    Scene scene;
    SceneStats stats;
};

// Builds the scene of one plane from the maps and trace of path 1.
[[nodiscard]] BuiltScene build_scene(const pic::PictureMaps& maps, const pic::DrawTrace& trace, Plane plane);

} // namespace sci::vector
