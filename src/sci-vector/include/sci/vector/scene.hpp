// SPDX-License-Identifier: GPL-3.0-or-later
//
// The vector scene of one plane of a picture (spec sections 6.3 and 6.4):
// a partition of the picture area into faces, and layers drawn over it in
// the order of the PIC commands. Coordinates are picture coordinates in
// original pixels: x to the right, y downwards, the picture area is
// [0, 320) x [0, 190), and pixel (x, y) covers [x, x + 1) x [y, y + 1), so
// its centre is (x + 0.5, y + 0.5).
#pragma once

#include <algorithm>
#include <cstdint>
#include <variant>
#include <vector>

namespace sci::vector {

inline constexpr int picture_width = 320;
inline constexpr int picture_height = 190;
inline constexpr std::size_t picture_pixels = static_cast<std::size_t>(picture_width) * picture_height;

// What a scene describes: the visual plane (values are EGA colour pairs,
// sci::pic::ColorPair) or the priority plane (values 0-15).
enum class Plane : std::int8_t { Visual, Priority };

struct Vec2 {
    double x = 0;
    double y = 0;

    friend bool operator==(const Vec2&, const Vec2&) = default;
};

// How a face is drawn.
enum class FaceFill : std::int8_t {
    Uniform, // In its value.
    Blocks,  // Fallback: each point takes the block value of the 1x pixel it lies in.
};

struct Face {
    FaceFill fill = FaceFill::Uniform;
    std::uint8_t value = 0;
};

// A boundary between faces, for scanline rasterisation: top.y < bottom.y,
// and face_after is the face on the +x side of the edge. Horizontal
// boundaries are left out, as no scanline crosses them. Every scanline
// through the picture area crosses the left picture edge first, so every
// point of the area lies after some edge.
struct ScanEdge {
    Vec2 top;
    Vec2 bottom;
    std::int32_t face_after = 0;
};

// A straight cut through a stroke: the stroke shows only where
// (p - point) . normal <= 0. Cuts end lines exactly at the contour they end
// on (spec section 6.10).
struct Cut {
    Vec2 point;
    Vec2 normal;

    [[nodiscard]] bool keeps(Vec2 p) const { return ((p.x - point.x) * normal.x) + ((p.y - point.y) * normal.y) <= 0; }
};

// A stroke along a polyline (a PIC line, or a curve fitted to a chain of
// lines), 1 original pixel wide: every point within `radius` of the
// polyline that every cut keeps. A line may have several strokes.
struct Stroke {
    std::int32_t source = -1; // Index of the primitive in the draw trace (the first, for a chain).
    std::vector<Vec2> points; // At least one point; one point is a dot.
    double radius = 0.5;
    std::uint8_t value = 0;
    bool fitted = false; // Along a curve fitted to a chain of lines.
    std::vector<Cut> cuts;

    [[nodiscard]] bool kept(Vec2 p) const {
        return std::ranges::all_of(cuts, [&](const Cut& cut) { return cut.keeps(p); });
    }
};

// A solid circle brush, drawn as the ellipse inscribed in its box.
struct Ellipse {
    std::int32_t source = -1; // Index of the primitive in the draw trace (the first, for a chain).
    Vec2 centre;
    double radius_x = 0;
    double radius_y = 0;
    std::uint8_t value = 0;
};

// A solid rectangle brush: [left, right) x [top, bottom).
struct Box {
    std::int32_t source = -1; // Index of the primitive in the draw trace (the first, for a chain).
    double left = 0;
    double top = 0;
    double right = 0;
    double bottom = 0;
    std::uint8_t value = 0;
};

// 1x pixels drawn as N x N blocks: textured brushes, and fallbacks.
struct Blocks {
    std::int32_t source = -1;          // Index of the primitive in the draw trace (the first, for a chain).
    std::vector<std::uint32_t> pixels; // Picture pixel indices, y * picture_width + x, ascending.
    std::vector<std::uint8_t> values;  // One per pixel.
};

using Layer = std::variant<Stroke, Ellipse, Box, Blocks>;

struct Scene {
    Plane plane = Plane::Visual;
    std::vector<Face> faces;
    std::vector<ScanEdge> edges;
    // Per picture pixel: the value a `blocks` face shows in that pixel.
    std::vector<std::uint8_t> block_values = std::vector<std::uint8_t>(picture_pixels, 0);
    std::vector<Layer> layers; // Bottom first.
};

} // namespace sci::vector
