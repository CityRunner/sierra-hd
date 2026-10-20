// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/render/rasterize.hpp"

#include "sci/core/assert.hpp"
#include "sci/core/rounding.hpp"
#include "sci/render/colors.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <variant>

namespace sci::render {

namespace {

using vector::picture_height;
using vector::picture_width;
using vector::Vec2;

constexpr int samples_per_pixel = 16; // Per axis.

// A closed or half-open interval of x on one row.
struct Span {
    double left = 0;
    double right = 0;
    bool closed = true; // Whether right belongs to the span.
};

// The rows [first, last] of the picture an object between top and bottom
// can touch.
std::pair<int, int> row_range(double top, double bottom) {
    const auto first = static_cast<int>(std::max<std::int64_t>(0, core::floor_to_int(top)));
    const auto last = static_cast<int>(std::min<std::int64_t>(picture_height - 1, core::floor_to_int(bottom)));
    return {first, last};
}

// Where the disc of radius r around c meets row y.
std::optional<Span> disc_span(Vec2 c, double r, double y) {
    const double dy = y - c.y;
    const double h = (r * r) - (dy * dy);
    if (h < 0) {
        return std::nullopt;
    }
    const double w = std::sqrt(h);
    return Span{.left = c.x - w, .right = c.x + w};
}

// Where the points within r of the segment p-q (its band, without the end
// discs) meet row y.
std::optional<Span> band_span(Vec2 p, Vec2 q, double r, double y) {
    const double dx = q.x - p.x;
    const double dy = q.y - p.y;
    const double length_squared = (dx * dx) + (dy * dy);
    if (length_squared == 0) {
        return std::nullopt; // The end discs cover it.
    }
    // Distance condition |dx * (y - p.y) - dy * (x - p.x)| <= r * |d|, and
    // projection condition 0 <= dx * (x - p.x) + dy * (y - p.y) <= |d|^2.
    double left = -1e300;
    double right = 1e300;
    const double limit = r * std::sqrt(length_squared);
    const double base = dx * (y - p.y);
    if (dy == 0) {
        if (std::abs(base) > limit) {
            return std::nullopt;
        }
    } else {
        const double a = p.x + ((base - limit) / dy);
        const double b = p.x + ((base + limit) / dy);
        left = std::min(a, b);
        right = std::max(a, b);
    }
    const double along = dy * (y - p.y);
    if (dx == 0) {
        if (along < 0 || along > length_squared) {
            return std::nullopt;
        }
    } else {
        const double a = p.x - (along / dx);
        const double b = p.x + ((length_squared - along) / dx);
        left = std::max(left, std::min(a, b));
        right = std::min(right, std::max(a, b));
    }
    if (left > right) {
        return std::nullopt;
    }
    return Span{.left = left, .right = right};
}

void extend(std::optional<Span>& total, const std::optional<Span>& part) {
    if (!part) {
        return;
    }
    if (!total) {
        total = part;
        return;
    }
    total->left = std::min(total->left, part->left);
    total->right = std::max(total->right, part->right);
}

// Spans of a layer on row y (in picture coordinates), passed to emit.
template <typename Emit>
void layer_spans(const vector::Layer& layer, double y, Emit emit) {
    if (const auto* stroke = std::get_if<vector::Stroke>(&layer)) {
        // Each segment's capsule is convex, so it meets the row in one span.
        if (stroke->points.size() == 1) {
            if (const auto span = disc_span(stroke->points[0], stroke->radius, y)) {
                emit(*span, stroke->value);
            }
            return;
        }
        for (std::size_t i = 0; i + 1 < stroke->points.size(); ++i) {
            std::optional<Span> span = disc_span(stroke->points[i], stroke->radius, y);
            extend(span, disc_span(stroke->points[i + 1], stroke->radius, y));
            extend(span, band_span(stroke->points[i], stroke->points[i + 1], stroke->radius, y));
            if (span) {
                emit(*span, stroke->value);
            }
        }
    } else if (const auto* ellipse = std::get_if<vector::Ellipse>(&layer)) {
        const double t = (y - ellipse->centre.y) / ellipse->radius_y;
        if (t >= -1 && t <= 1) {
            const double w = ellipse->radius_x * std::sqrt(1 - (t * t));
            emit(Span{.left = ellipse->centre.x - w, .right = ellipse->centre.x + w}, ellipse->value);
        }
    } else if (const auto* box = std::get_if<vector::Box>(&layer)) {
        if (y >= box->top && y < box->bottom) {
            emit(Span{.left = box->left, .right = box->right, .closed = false}, box->value);
        }
    } else if (const auto* blocks = std::get_if<vector::Blocks>(&layer)) {
        const auto row = static_cast<std::uint32_t>(core::floor_to_int(y));
        const auto begin = std::ranges::lower_bound(blocks->pixels, row * picture_width);
        const auto end = std::ranges::lower_bound(blocks->pixels, (row + 1) * picture_width);
        for (auto it = begin; it != end; ++it) {
            const auto x = static_cast<double>(*it % picture_width);
            const auto index = static_cast<std::size_t>(it - blocks->pixels.begin());
            emit(Span{.left = x, .right = x + 1, .closed = false}, blocks->values[index]);
        }
    }
}

// Adds the colours of one row of a pixel's samples to its sums. Rows of a
// single value, the common case, take one addition.
void add_samples(std::span<const std::uint8_t> samples, std::array<std::uint32_t, 3>& sum) {
    const std::uint8_t first = samples[0];
    if (std::ranges::all_of(samples, [first](std::uint8_t value) { return value == first; })) {
        const PairSum& color = pair_sums[first];
        const auto n = static_cast<std::uint32_t>(samples.size());
        sum[0] += color.r * n;
        sum[1] += color.g * n;
        sum[2] += color.b * n;
        return;
    }
    for (const std::uint8_t value : samples) {
        const PairSum& color = pair_sums[value];
        sum[0] += color.r;
        sum[1] += color.g;
        sum[2] += color.b;
    }
}

// The rows a layer can touch.
std::pair<int, int> layer_rows(const vector::Layer& layer) {
    if (const auto* stroke = std::get_if<vector::Stroke>(&layer)) {
        double top = stroke->points.front().y;
        double bottom = top;
        for (const Vec2& p : stroke->points) {
            top = std::min(top, p.y);
            bottom = std::max(bottom, p.y);
        }
        return row_range(top - stroke->radius, bottom + stroke->radius);
    }
    if (const auto* ellipse = std::get_if<vector::Ellipse>(&layer)) {
        return row_range(ellipse->centre.y - ellipse->radius_y, ellipse->centre.y + ellipse->radius_y);
    }
    if (const auto* box = std::get_if<vector::Box>(&layer)) {
        return row_range(box->top, box->bottom);
    }
    const auto& blocks = std::get<vector::Blocks>(layer);
    if (blocks.pixels.empty()) {
        return {1, 0};
    }
    return {static_cast<int>(blocks.pixels.front() / picture_width),
            static_cast<int>(blocks.pixels.back() / picture_width)};
}

// The sample indices i with (i + 0.5) / density in the span.
std::pair<std::int64_t, std::int64_t> sample_range(const Span& span, int density, std::int64_t count) {
    const double d = density;
    const std::int64_t first = std::max<std::int64_t>(0, core::ceil_to_int((span.left * d) - 0.5));
    const std::int64_t last =
        span.closed ? core::floor_to_int((span.right * d) - 0.5) : core::ceil_to_int((span.right * d) - 0.5) - 1;
    return {first, std::min(last, count - 1)};
}

} // namespace

SceneSampler::SceneSampler(const vector::Scene& scene)
    : scene_(&scene), edges_by_row_(picture_height), layers_by_row_(picture_height) {
    for (std::size_t i = 0; i < scene.edges.size(); ++i) {
        const vector::ScanEdge& edge = scene.edges[i];
        const auto [first, last] = row_range(edge.top.y, edge.bottom.y);
        for (int row = first; row <= last; ++row) {
            edges_by_row_[static_cast<std::size_t>(row)].push_back(static_cast<std::uint32_t>(i));
        }
    }
    for (std::size_t i = 0; i < scene.layers.size(); ++i) {
        const auto [first, last] = layer_rows(scene.layers[i]);
        for (int row = first; row <= last; ++row) {
            layers_by_row_[static_cast<std::size_t>(row)].push_back(static_cast<std::uint32_t>(i));
        }
    }
}

void SceneSampler::crossings(double y, std::vector<Crossing>& found) const {
    found.clear();
    const auto row = static_cast<std::size_t>(core::floor_to_int(y));
    for (const std::uint32_t index : edges_by_row_[row]) {
        const vector::ScanEdge& edge = scene_->edges[index];
        if (y < edge.top.y || y >= edge.bottom.y) {
            continue;
        }
        const double slope = (edge.bottom.x - edge.top.x) / (edge.bottom.y - edge.top.y);
        found.push_back(
            Crossing{.x = edge.top.x + ((y - edge.top.y) * slope), .slope = slope, .face = edge.face_after});
    }
    // Edges through the same point: the one closest to +x decides what
    // follows, so it comes last.
    std::ranges::stable_sort(found, [](const Crossing& a, const Crossing& b) {
        if (a.x != b.x) {
            return a.x < b.x;
        }
        if (a.slope != b.slope) {
            return a.slope < b.slope;
        }
        return a.face < b.face;
    });
}

// The first sample index i, at or after `from`, whose point (i + 0.5) /
// density lies at or after x.
std::size_t SceneSampler::first_sample_at(double x, int density, std::size_t from, std::size_t count) {
    const auto at = [&](std::size_t i) { return x <= (static_cast<double>(i) + 0.5) / density; };
    auto i = static_cast<std::size_t>(std::clamp<std::int64_t>(
        core::ceil_to_int((x * density) - 0.5), static_cast<std::int64_t>(from), static_cast<std::int64_t>(count)));
    while (i > from && at(i - 1)) {
        --i;
    }
    while (i < count && !at(i)) {
        ++i;
    }
    return i;
}

void SceneSampler::face_runs(double y, int density, std::size_t count, std::vector<FaceRun>& runs) const {
    SCI_ASSERT(y >= 0 && y < picture_height);
    SCI_ASSERT(count == static_cast<std::size_t>(picture_width) * static_cast<std::size_t>(density));
    std::vector<Crossing> found;
    crossings(y, found);
    runs.clear();
    std::size_t start = 0;
    for (const Crossing& crossing : found) {
        start = first_sample_at(crossing.x, density, start, count);
        if (!runs.empty() && runs.back().start == start) {
            runs.back().face = crossing.face; // The later crossing decides.
        } else {
            runs.push_back(FaceRun{.start = start, .face = crossing.face});
        }
    }
    SCI_ASSERT(!runs.empty() && runs.front().start == 0);
}

void SceneSampler::sample_faces(double y, int density, std::span<std::int32_t> faces) const {
    std::vector<FaceRun> runs;
    face_runs(y, density, faces.size(), runs);
    for (std::size_t k = 0; k < runs.size(); ++k) {
        const std::size_t end = k + 1 < runs.size() ? runs[k + 1].start : faces.size();
        std::fill(faces.begin() + static_cast<std::ptrdiff_t>(runs[k].start),
                  faces.begin() + static_cast<std::ptrdiff_t>(end), runs[k].face);
    }
}

void SceneSampler::sample_sources(double y, int density, std::span<std::int32_t> sources) const {
    sample_faces(y, density, sources);
    for (std::int32_t& source : sources) {
        SCI_ASSERT(source >= 0);
        source = -1 - source;
    }
    const auto row = static_cast<std::size_t>(core::floor_to_int(y));
    for (const std::uint32_t index : layers_by_row_[row]) {
        layer_spans(scene_->layers[index], y, [&](const Span& span, std::uint8_t /*value*/) {
            const auto [first, last] = sample_range(span, density, static_cast<std::int64_t>(sources.size()));
            for (std::int64_t i = first; i <= last; ++i) {
                sources[static_cast<std::size_t>(i)] = static_cast<std::int32_t>(index);
            }
        });
    }
}

void SceneSampler::sample_row(double y, int density, std::span<std::uint8_t> values) const {
    const std::size_t count = values.size();
    std::vector<FaceRun> runs;
    face_runs(y, density, count, runs);
    const auto row = static_cast<std::size_t>(core::floor_to_int(y));
    const auto per_pixel = static_cast<std::size_t>(density);
    for (std::size_t k = 0; k < runs.size(); ++k) {
        const std::size_t end = k + 1 < runs.size() ? runs[k + 1].start : count;
        const vector::Face& face = scene_->faces[static_cast<std::size_t>(runs[k].face)];
        if (face.fill == vector::FaceFill::Uniform) {
            std::fill(values.begin() + static_cast<std::ptrdiff_t>(runs[k].start),
                      values.begin() + static_cast<std::ptrdiff_t>(end), face.value);
            continue;
        }
        for (std::size_t i = runs[k].start; i < end; ++i) {
            values[i] = scene_->block_values[(row * picture_width) + (i / per_pixel)];
        }
    }
    for (const std::uint32_t index : layers_by_row_[row]) {
        layer_spans(scene_->layers[index], y, [&](const Span& span, std::uint8_t value) {
            const auto [first, last] = sample_range(span, density, static_cast<std::int64_t>(count));
            if (first <= last) {
                std::fill(values.begin() + first, values.begin() + last + 1, value);
            }
        });
    }
}

RgbaImage render_hd(const vector::Scene& visual, int scale, core::Executor& executor) {
    SCI_ASSERT(scale >= 1);
    const SceneSampler sampler(visual);
    RgbaImage image{.width = picture_width * scale, .height = picture_height * scale, .pixels = {}};
    image.pixels.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4);
    const int density = samples_per_pixel * scale;
    const auto width = static_cast<std::size_t>(image.width);
    executor.run(static_cast<std::size_t>(image.height), [&](std::size_t row) {
        std::vector<std::uint8_t> values(width * samples_per_pixel);
        std::vector<std::array<std::uint32_t, 3>> sums(width, {0, 0, 0});
        for (int sub = 0; sub < samples_per_pixel; ++sub) {
            const double y =
                ((static_cast<double>(row) * samples_per_pixel) + sub + 0.5) / static_cast<double>(density);
            sampler.sample_row(y, density, values);
            for (std::size_t x = 0; x < width; ++x) {
                add_samples(std::span(values).subspan(x * samples_per_pixel, samples_per_pixel), sums[x]);
            }
        }
        // Each sum covers 256 samples of two colours: divide by 512, halves
        // upwards.
        const std::span<std::uint8_t> out = std::span(image.pixels).subspan(row * width * 4, width * 4);
        for (std::size_t x = 0; x < width; ++x) {
            for (std::size_t c = 0; c < 3; ++c) {
                out[(x * 4) + c] = to_8bit((sums[x][c] + 256) >> 9U);
            }
            out[(x * 4) + 3] = 255;
        }
    });
    return image;
}

std::vector<std::uint8_t> render_priority(const vector::Scene& priority, int scale, core::Executor& executor) {
    SCI_ASSERT(scale >= 1);
    const SceneSampler sampler(priority);
    const auto width = static_cast<std::size_t>(picture_width) * static_cast<std::size_t>(scale);
    const auto height = static_cast<std::size_t>(picture_height) * static_cast<std::size_t>(scale);
    std::vector<std::uint8_t> map(width * height, 0);
    executor.run(height, [&](std::size_t row) {
        const double y = (static_cast<double>(row) + 0.5) / scale;
        sampler.sample_row(y, scale, std::span(map).subspan(row * width, width));
    });
    return map;
}

std::vector<std::uint8_t> reduce_to_1x(const vector::Scene& scene) {
    const SceneSampler sampler(scene);
    std::vector<std::uint8_t> reduced(vector::picture_pixels, 0);
    std::vector<std::uint8_t> values(static_cast<std::size_t>(picture_width) * samples_per_pixel);
    std::vector<std::array<std::uint16_t, 256>> counts(picture_width);
    for (int row = 0; row < picture_height; ++row) {
        for (auto& count : counts) {
            count.fill(0);
        }
        for (int sub = 0; sub < samples_per_pixel; ++sub) {
            const double y = row + ((sub + 0.5) / samples_per_pixel);
            sampler.sample_row(y, samples_per_pixel, values);
            for (std::size_t i = 0; i < values.size(); ++i) {
                ++counts[i / samples_per_pixel][values[i]];
            }
        }
        for (std::size_t x = 0; x < counts.size(); ++x) {
            // max_element returns the first maximum: the smallest value.
            const auto best = std::distance(counts[x].begin(), std::ranges::max_element(counts[x]));
            reduced[(static_cast<std::size_t>(row) * picture_width) + x] = static_cast<std::uint8_t>(best);
        }
    }
    return reduced;
}

} // namespace sci::render
