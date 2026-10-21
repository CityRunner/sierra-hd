// SPDX-License-Identifier: GPL-3.0-or-later
//
// HD pictures for the game screen: the vector path's rendering of a picture
// as kDrawPic drew it (mirrored, EGA palette), at N, keeping the last few.
#pragma once

#include "sci/core/executor.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/resource/resource_manager.hpp"

#include <filesystem>
#include <list>
#include <optional>

namespace sci::app {

class HdPictures {
public:
    HdPictures(const resource::ResourceManager& manager, std::optional<std::filesystem::path> hints,
               core::Executor& executor)
        : manager_(&manager), hints_(std::move(hints)), executor_(&executor) {}

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

    const resource::ResourceManager* manager_;
    std::optional<std::filesystem::path> hints_;
    core::Executor* executor_;
    std::list<Entry> cache_; // Most recent first.
};

} // namespace sci::app
