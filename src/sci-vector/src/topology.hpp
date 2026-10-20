// SPDX-License-Identifier: GPL-3.0-or-later
//
// The planar graph of a picture plane (spec section 6.3), in exact integer
// arithmetic. Coordinates are doubled picture coordinates, so that pixel
// centres, where PIC lines start and end, are integers: pixel (x, y) has
// its centre at (2x + 1, 2y + 1).
//
// Range: every input coordinate lies in [-max_coordinate, max_coordinate].
// Directions are then at most 2^11, cross products below 2^24, so
// intersection points have denominators below 2^24 and numerators below
// 2^36, and comparing two points multiplies to below 2^60.
#pragma once

#include "sci/core/exact.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sci::vector::detail {

inline constexpr std::int64_t max_coordinate = 1024;

// A segment of the line through the integer points a and b (a != b),
// between the points from and to on that line.
struct TopoSegment {
    core::IntPoint a;
    core::IntPoint b;
    core::RationalPoint from;
    core::RationalPoint to;
};

// A segment from a to b.
[[nodiscard]] TopoSegment make_segment(core::IntPoint a, core::IntPoint b);

// Whether two segments have a point in common.
[[nodiscard]] bool segments_touch(const TopoSegment& s, const TopoSegment& t);

// Where the line through a and b crosses the segment, if it does in a
// single point.
[[nodiscard]] std::optional<core::RationalPoint> line_meets_segment(core::IntPoint a, core::IntPoint b,
                                                                    const TopoSegment& segment);

// The foot of the perpendicular from p to the segment's line, if it lies on
// the segment.
[[nodiscard]] std::optional<core::RationalPoint> perpendicular_foot(core::IntPoint p, const TopoSegment& segment);

struct HalfEdge {
    std::int32_t origin = 0; // Vertex index.
    std::int32_t twin = 0;
    std::int32_t next = 0; // The following half-edge around the same face.
    std::int32_t face = 0;
    core::IntPoint direction; // Primitive integer direction from origin to the twin's origin.
};

// The arrangement of the segments: vertices at all end points and
// intersections, edges between them (overlapping segments share edges),
// and faces as the cycles of half-edges. A face lies to the right of its
// half-edges as seen on screen (y downwards). Each connected component's
// outer cycle is a face of its own; it stands for the part of the
// enclosing face next to the component.
struct Arrangement {
    std::vector<core::RationalPoint> vertices;
    std::vector<HalfEdge> half_edges; // Twins are adjacent: 2i and 2i + 1.
    std::int32_t face_count = 0;
};

[[nodiscard]] Arrangement build_arrangement(std::span<const TopoSegment> segments);

// The face of every pixel centre of a width x height grid (pixel (x, y) at
// doubled point (2x + 1, 2y + 1)), row by row; on_edge where the centre lies
// on an edge, outside where no edge lies to its left.
inline constexpr std::int32_t on_edge = -1;
inline constexpr std::int32_t outside = -2;

[[nodiscard]] std::vector<std::int32_t> label_pixel_centres(const Arrangement& arrangement, int width, int height);

} // namespace sci::vector::detail
