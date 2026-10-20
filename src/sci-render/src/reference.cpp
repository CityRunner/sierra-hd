// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/render/reference.hpp"

#include "sci/core/assert.hpp"
#include "sci/core/rounding.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <variant>

namespace sci::render {

namespace {

using vector::Vec2;

double distance_to_segment(Vec2 p, Vec2 a, Vec2 b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_squared = (dx * dx) + (dy * dy);
    const double t =
        length_squared == 0 ? 0 : std::clamp((((p.x - a.x) * dx) + ((p.y - a.y) * dy)) / length_squared, 0.0, 1.0);
    const double ex = p.x - a.x - (t * dx);
    const double ey = p.y - a.y - (t * dy);
    return std::sqrt((ex * ex) + (ey * ey));
}

// The layer's value at p, if it covers p.
std::optional<std::uint8_t> layer_value(const vector::Layer& layer, Vec2 p) {
    if (const auto* stroke = std::get_if<vector::Stroke>(&layer)) {
        const auto& points = stroke->points;
        bool covered = points.size() == 1 && distance_to_segment(p, points[0], points[0]) <= stroke->radius;
        for (std::size_t i = 0; i + 1 < points.size() && !covered; ++i) {
            covered = distance_to_segment(p, points[i], points[i + 1]) <= stroke->radius;
        }
        return covered ? std::optional(stroke->value) : std::nullopt;
    }
    if (const auto* ellipse = std::get_if<vector::Ellipse>(&layer)) {
        const double u = (p.x - ellipse->centre.x) / ellipse->radius_x;
        const double v = (p.y - ellipse->centre.y) / ellipse->radius_y;
        return (u * u) + (v * v) <= 1 ? std::optional(ellipse->value) : std::nullopt;
    }
    if (const auto* box = std::get_if<vector::Box>(&layer)) {
        const bool inside = p.x >= box->left && p.x < box->right && p.y >= box->top && p.y < box->bottom;
        return inside ? std::optional(box->value) : std::nullopt;
    }
    const auto& blocks = std::get<vector::Blocks>(layer);
    const auto pixel =
        static_cast<std::uint32_t>((core::floor_to_int(p.y) * vector::picture_width) + core::floor_to_int(p.x));
    const auto it = std::ranges::lower_bound(blocks.pixels, pixel);
    if (it != blocks.pixels.end() && *it == pixel) {
        return blocks.values[static_cast<std::size_t>(it - blocks.pixels.begin())];
    }
    return std::nullopt;
}

} // namespace

std::uint8_t reference_value(const vector::Scene& scene, Vec2 p) {
    for (const vector::Layer& layer : std::views::reverse(scene.layers)) {
        if (const auto value = layer_value(layer, p)) {
            return *value;
        }
    }
    // The face after the last edge crossing the row at or left of p; of
    // edges through the same point, the one closest to +x.
    std::optional<std::int32_t> face;
    double best_x = 0;
    double best_slope = 0;
    for (const vector::ScanEdge& edge : scene.edges) {
        if (p.y < edge.top.y || p.y >= edge.bottom.y) {
            continue;
        }
        const double slope = (edge.bottom.x - edge.top.x) / (edge.bottom.y - edge.top.y);
        const double x = edge.top.x + ((p.y - edge.top.y) * slope);
        if (x > p.x) {
            continue;
        }
        if (!face || x > best_x || (x == best_x && slope > best_slope) ||
            (x == best_x && slope == best_slope && edge.face_after > *face)) {
            face = edge.face_after;
            best_x = x;
            best_slope = slope;
        }
    }
    SCI_ASSERT(face.has_value());
    const vector::Face& found = scene.faces[static_cast<std::size_t>(*face)];
    if (found.fill == vector::FaceFill::Uniform) {
        return found.value;
    }
    return scene.block_values[(static_cast<std::size_t>(core::floor_to_int(p.y)) * vector::picture_width) +
                              static_cast<std::size_t>(core::floor_to_int(p.x))];
}

} // namespace sci::render
