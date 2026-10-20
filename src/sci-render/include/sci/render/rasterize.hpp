// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rasterisation of vector scenes (spec section 6.6). Every sample takes the
// value of the topmost layer covering it, or of the face it lies in, so the
// faces partition the picture without gaps or overlaps. HD pixels average
// 16 x 16 samples; colours are averaged in integers, so the result is the
// same on every machine.
#pragma once

#include "sci/core/executor.hpp"
#include "sci/vector/scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sci::render {

// The scene prepared for sampling: edges and layers indexed by picture row.
class SceneSampler {
public:
    explicit SceneSampler(const vector::Scene& scene);

    // The scene's value at the points ((i + 0.5) / density, y) for every i
    // in [0, picture_width * density). y lies in [0, picture_height).
    void sample_row(double y, int density, std::span<std::uint8_t> values) const;

    // As sample_row, but the face index at each point (layers are ignored).
    void sample_faces(double y, int density, std::span<std::int32_t> faces) const;

    // As sample_row, but what decides each point: the index of the topmost
    // layer covering it, or -1 - f for face f.
    void sample_sources(double y, int density, std::span<std::int32_t> sources) const;

private:
    struct Crossing {
        double x = 0;
        double slope = 0; // dx / dy
        std::int32_t face = 0;
    };

    // From each run's start (a sample index) on, the face of the run.
    struct FaceRun {
        std::size_t start = 0;
        std::int32_t face = 0;
    };

    void crossings(double y, std::vector<Crossing>& found) const;
    void face_runs(double y, int density, std::size_t count, std::vector<FaceRun>& runs) const;
    [[nodiscard]] static std::size_t first_sample_at(double x, int density, std::size_t from, std::size_t count);

    const vector::Scene* scene_;
    std::vector<std::vector<std::uint32_t>> edges_by_row_;
    std::vector<std::vector<std::uint32_t>> layers_by_row_; // Ascending: drawing order.
};

// An image with 8-bit RGBA pixels, row by row.
struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// The picture area of a visual scene at 320N x 190N.
[[nodiscard]] RgbaImage render_hd(const vector::Scene& visual, int scale, core::Executor& executor);

// The HD priority map of a priority scene at 320N x 190N: the value at each
// pixel's centre.
[[nodiscard]] std::vector<std::uint8_t> render_priority(const vector::Scene& priority, int scale,
                                                        core::Executor& executor);

// The scene reduced to 1x for the acceptance check (spec section 6.4): per
// picture pixel the value covering most of its 16 x 16 samples (the
// smallest value on a tie).
[[nodiscard]] std::vector<std::uint8_t> reduce_to_1x(const vector::Scene& scene);

} // namespace sci::render
