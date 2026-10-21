// SPDX-License-Identifier: GPL-3.0-or-later
//
// HD pictures for the game screen: the vector path's rendering of a picture
// as kDrawPic drew it (mirrored, EGA palette), at N, keeping the last few.
// The scenes come from the analysis cache.
#pragma once

#include "sci/core/executor.hpp"
#include "sci/platform/analysis_cache.hpp"
#include "sci/render/rasterize.hpp"

#include <list>
#include <optional>

namespace sci::app {

class HdPictures {
public:
    HdPictures(platform::AnalysisCache& analysis, core::Executor& executor)
        : analysis_(&analysis), executor_(&executor) {}

    // The picture at 320N x 190N, or null if it cannot be drawn.
    const render::RgbaImage* get(int picture, bool mirrored, int ega_palette, int scale);

private:
    struct Entry {
        int picture = 0;
        bool mirrored = false;
        int ega_palette = 0;
        int scale = 0;
        std::optional<render::RgbaImage> image;
    };

    platform::AnalysisCache* analysis_;
    core::Executor* executor_;
    std::list<Entry> cache_; // Most recent first.
};

} // namespace sci::app
