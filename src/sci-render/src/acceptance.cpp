// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/render/acceptance.hpp"

#include "sci/core/assert.hpp"
#include "sci/render/colors.hpp"
#include "sci/render/rasterize.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace sci::render {

namespace {

using vector::picture_height;
using vector::picture_pixels;
using vector::picture_width;
using vector::Vec2;

constexpr int samples_per_pixel = 16;
constexpr int min_swap_pixels = 4;

std::uint32_t pair_class(std::uint8_t pair) {
    const Rgb8 color = pair_color(pair);
    return (std::uint32_t{color.r} << 16U) | (std::uint32_t{color.g} << 8U) | color.b;
}

std::size_t index(int x, int y) {
    return (static_cast<std::size_t>(y) * picture_width) + static_cast<std::size_t>(x);
}

// Pixels with a neighbour of another class, among their eight.
std::vector<bool> edge_band(const std::vector<std::uint32_t>& classes) {
    std::vector<bool> band(picture_pixels, false);
    for (int y = 0; y < picture_height; ++y) {
        for (int x = 0; x < picture_width; ++x) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx >= 0 && nx < picture_width && ny >= 0 && ny < picture_height &&
                        classes[index(nx, ny)] != classes[index(x, y)]) {
                        band[index(x, y)] = true;
                    }
                }
            }
        }
    }
    return band;
}

// Groups of at least min_swap_pixels 4-connected marked pixels.
int count_swaps(std::vector<bool> marked) {
    int swaps = 0;
    std::vector<std::size_t> pending;
    for (std::size_t start = 0; start < picture_pixels; ++start) {
        if (!marked[start]) {
            continue;
        }
        marked[start] = false;
        pending.assign(1, start);
        int size = 0;
        while (!pending.empty()) {
            const std::size_t i = pending.back();
            pending.pop_back();
            ++size;
            const int x = static_cast<int>(i % picture_width);
            const int y = static_cast<int>(i / picture_width);
            for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx >= 0 && nx < picture_width && ny >= 0 && ny < picture_height && marked[index(nx, ny)]) {
                    marked[index(nx, ny)] = false;
                    pending.push_back(index(nx, ny));
                }
            }
        }
        swaps += size >= min_swap_pixels ? 1 : 0;
    }
    return swaps;
}

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

bool within_stroke(Vec2 p, const vector::Stroke& stroke) {
    if (stroke.points.size() == 1) {
        return distance_to_segment(p, stroke.points[0], stroke.points[0]) <= stroke.radius;
    }
    for (std::size_t i = 0; i + 1 < stroke.points.size(); ++i) {
        if (distance_to_segment(p, stroke.points[i], stroke.points[i + 1]) <= stroke.radius) {
            return true;
        }
    }
    return false;
}

// Whether the stroke covers at least one of the pixel's samples (the
// centre first, as it usually does).
bool touches(const vector::Stroke& stroke, int x, int y) {
    if (within_stroke({.x = x + 0.5, .y = y + 0.5}, stroke)) {
        return true;
    }
    for (int sy = 0; sy < samples_per_pixel; ++sy) {
        for (int sx = 0; sx < samples_per_pixel; ++sx) {
            const Vec2 p{.x = x + ((sx + 0.5) / samples_per_pixel), .y = y + ((sy + 0.5) / samples_per_pixel)};
            if (within_stroke(p, stroke)) {
                return true;
            }
        }
    }
    return false;
}

// For every primitive of the trace, the index of its stroke layer, or -1.
std::vector<std::int32_t> stroke_layers(const vector::Scene& scene, std::size_t primitives) {
    std::vector<std::int32_t> layers(primitives, -1);
    for (std::size_t i = 0; i < scene.layers.size(); ++i) {
        if (const auto* stroke = std::get_if<vector::Stroke>(&scene.layers[i]);
            stroke != nullptr && stroke->source >= 0 && std::cmp_less(stroke->source, primitives)) {
            layers[static_cast<std::size_t>(stroke->source)] = static_cast<std::int32_t>(i);
        }
    }
    return layers;
}

void check_lines(const vector::Scene& scene, const pic::DrawTrace& trace, Acceptance& result) {
    const std::vector<std::int32_t> layer_of = stroke_layers(scene, trace.primitives.size());
    const auto owner_layer = [&](int x, int y) {
        const std::int32_t owner = trace.visual_owner[pic::PictureMaps::index(x, y + pic::picture_top)];
        return owner < 0 ? -1 : layer_of[static_cast<std::size_t>(owner)];
    };
    // Whether any sample of each pixel shows its line's stroke.
    std::vector<bool> shown(picture_pixels, false);
    const SceneSampler sampler(scene);
    std::vector<std::int32_t> sources(static_cast<std::size_t>(picture_width) * samples_per_pixel);
    for (int row = 0; row < picture_height * samples_per_pixel; ++row) {
        const int y = row / samples_per_pixel;
        sampler.sample_sources((row + 0.5) / samples_per_pixel, samples_per_pixel, sources);
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const int x = static_cast<int>(i / samples_per_pixel);
            const std::int32_t layer = owner_layer(x, y);
            if (layer >= 0 && sources[i] == layer) {
                shown[index(x, y)] = true;
            }
        }
    }
    for (int y = 0; y < picture_height; ++y) {
        for (int x = 0; x < picture_width; ++x) {
            const std::int32_t layer = owner_layer(x, y);
            if (layer < 0) {
                continue;
            }
            ++result.line_pixels;
            if (!touches(std::get<vector::Stroke>(scene.layers[static_cast<std::size_t>(layer)]), x, y)) {
                ++result.untouched_line_pixels;
            } else if (!shown[index(x, y)]) {
                ++result.overlapped_line_pixels;
            }
        }
    }
}

} // namespace

Acceptance check_acceptance(const vector::Scene& visual, const pic::PictureMaps& maps, const pic::DrawTrace& trace) {
    SCI_ASSERT(visual.plane == vector::Plane::Visual);
    Acceptance result;
    const std::vector<std::uint8_t> reduced = reduce_to_1x(visual);
    std::vector<std::uint32_t> classes(picture_pixels);
    for (int y = 0; y < picture_height; ++y) {
        for (int x = 0; x < picture_width; ++x) {
            classes[index(x, y)] = pair_class(maps.pairs[pic::PictureMaps::index(x, y + pic::picture_top)]);
        }
    }
    const std::vector<bool> band = edge_band(classes);
    std::vector<bool> differing(picture_pixels, false);
    for (std::size_t i = 0; i < picture_pixels; ++i) {
        if (band[i]) {
            continue;
        }
        ++result.compared_pixels;
        if (pair_class(reduced[i]) != classes[i]) {
            ++result.differing_pixels;
            differing[i] = true;
        }
    }
    result.swaps = count_swaps(std::move(differing));
    check_lines(visual, trace, result);
    return result;
}

} // namespace sci::render
