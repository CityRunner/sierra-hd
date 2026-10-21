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
#include "sci/vector/hints.hpp"
#include "sci/vector/scene.hpp"

#include <span>
#include <string>
#include <vector>

namespace sci::vector {

// A face whose pixels disagree, drawn from the 1x blocks: its region
// pixels and their bounding box (picture pixels, inclusive).
struct Fallback {
    int pixels = 0;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// What the analysis did with a hint: how it was used, or why not.
struct HintOutcome {
    int picture = 0; // PIC resource number.
    std::string id;
    std::string detail;
};

// A change made for stroke integrity and occlusion (spec section 6.10),
// with the evidence that justified it.
enum class IntegrityChangeKind : std::int8_t {
    ClosedJoint, // Two line ends moved to meet.
    ClippedEnd,  // A line end extended or trimmed to a contour and cut along it.
    MergedRun,   // A run of a line removed under the line the player saw there.
    KeptContact, // A line end on a contour left as drawn: the evidence did not allow a cut.
};

struct IntegrityChange {
    IntegrityChangeKind kind = IntegrityChangeKind::ClosedJoint;
    std::int32_t primitive = -1; // The line changed (trace index).
    std::int32_t other = -1;     // The line it meets, ends on or lies under.
    int x = 0;                   // A picture pixel where it happened.
    int y = 0;
    const char* evidence = "";
};

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
    int fallback_pixels = 0; // Region pixels in mixed faces, drawn as 1x blocks.
    int brushes = 0;         // Solid brushes drawn as shapes.
    int textured = 0;        // Textured brushes drawn as blocks.
    // Chains of short lines replaced by fitted curves.
    int straightened = 0;            // By one straight line.
    int arcs = 0;                    // By circular arcs.
    int splines = 0;                 // By cubic splines.
    int ellipses = 0;                // Closed chains, by ellipses.
    int fitted_lines = 0;            // PIC lines replaced by these curves.
    int rejected_fits = 0;           // Fits left out because they would change the faces.
    std::vector<Fallback> fallbacks; // The mixed faces.
    // Hints (spec section 6.7).
    int hint_chains = 0;                     // Chain hints fitted as they ask.
    int hint_gaps = 0;                       // Gap hints closed.
    std::vector<HintOutcome> applied_hints;  // Hints used, with what they changed.
    std::vector<HintOutcome> rejected_hints; // Hints left out; the automatic result stands there.
    // Stroke integrity and occlusion (spec section 6.10), visual plane.
    int closed_joints = 0;
    int clipped_ends = 0;
    int merged_runs = 0;
    int kept_contacts = 0; // Contacts left to the command order: the evidence did not allow a cut.
    std::vector<IntegrityChange> integrity_changes;
};

struct BuiltScene {
    Scene scene;
    SceneStats stats;
};

// The version of the analysis, part of the analysis cache's key (spec
// section 10.2): bump it with every change that changes the scenes
// build_scene makes, that is whenever tests/golden/pictures.txt changes.
inline constexpr std::uint32_t analyser_version = 1;

// Builds the scene of one plane from the maps and trace of path 1. `hints`
// holds the hints of each draw_picture call of the trace (by the trace's
// picture index; null or missing for none). Hints for lines not drawn into
// this plane are left out; the other plane's scene uses them.
[[nodiscard]] BuiltScene build_scene(const pic::PictureMaps& maps, const pic::DrawTrace& trace, Plane plane,
                                     std::span<const PictureHints* const> hints = {});

#ifdef SCI_DEBUG_HIGHLIGHT
// Development only (CMake option SCI_DEBUG_HIGHLIGHT, spec section 6.3): the
// scene with fitted strokes in light magenta and fallback blocks in yellow.
[[nodiscard]] Scene highlight_replacements(Scene scene);
#endif

} // namespace sci::vector
