// SPDX-License-Identifier: GPL-3.0-or-later
//
// The HD background and the HD priority map of the picture shown, at N
// (spec sections 6.6 and 6.8): the vector path's rendering of the picture
// stack as kDrawPic drew it (base and overlays, with their options), with
// the kernel lines drawn into it as strokes over it. The scenes come from
// the analysis cache; the last few renderings are kept.
#pragma once

#include "sci/core/executor.hpp"
#include "sci/platform/analysis_cache.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vm/shown_screen.hpp"

#include <cstdint>
#include <list>
#include <optional>
#include <span>
#include <vector>

namespace sci::platform {

// The picture area at 320N x 190N; null or empty where it cannot be drawn.
struct HdImages {
    const render::RgbaImage* background = nullptr;
    std::span<const std::uint8_t> priority;
};

// The scene variant of a picture stack (base first, then the overlays).
[[nodiscard]] SceneVariant stack_variant(std::span<const vm::ShownPicture> stack, vector::Plane plane);

// The visual scene with the kernel lines added as strokes (screen
// coordinates; the picture starts at row 10).
[[nodiscard]] vector::Scene with_lines(const vector::Scene& visual, std::span<const vm::ShownLine> lines);

class HdPictures {
public:
    HdPictures(AnalysisCache& analysis, core::Executor& executor) : analysis_(&analysis), executor_(&executor) {}

    // The images of `stack` with `lines`, at N; none for an empty stack.
    HdImages get(std::span<const vm::ShownPicture> stack, std::span<const vm::ShownLine> lines, int scale);

private:
    struct Entry {
        std::vector<vm::ShownPicture> stack;
        std::vector<vm::ShownLine> lines;
        int scale = 0;
        std::optional<render::RgbaImage> background;
        std::vector<std::uint8_t> priority;
    };

    AnalysisCache* analysis_;
    core::Executor* executor_;
    std::list<Entry> cache_; // Most recent first.
};

} // namespace sci::platform
