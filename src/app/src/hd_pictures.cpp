// SPDX-License-Identifier: GPL-3.0-or-later

#include "hd_pictures.hpp"

#include "sci/core/log.hpp"
#include "sci/vector/build_scene.hpp"

#include <algorithm>
#include <cstdint>
#include <format>

namespace sci::app {

namespace {

constexpr std::size_t kept = 4;

} // namespace

const render::RgbaImage* HdPictures::get(int picture, bool mirrored, int ega_palette, int scale) {
    const auto same = [&](const Entry& e) {
        return e.picture == picture && e.mirrored == mirrored && e.ega_palette == ega_palette && e.scale == scale;
    };
    if (const auto it = std::ranges::find_if(cache_, same); it != cache_.end()) {
        cache_.splice(cache_.begin(), cache_, it);
        const auto& image = cache_.front().image;
        return image ? &*image : nullptr;
    }
    Entry entry{.picture = picture, .mirrored = mirrored, .ega_palette = ega_palette, .scale = scale, .image = {}};
    const auto scene = analysis_->scene(platform::SceneVariant{
        .picture = static_cast<std::uint16_t>(picture),
        .plane = vector::Plane::Visual,
        .mirrored = mirrored,
        .ega_palette = static_cast<std::uint8_t>(ega_palette),
    });
    if (scene) {
        entry.image = render::render_hd(**scene, scale, *executor_);
    } else {
        core::log(core::LogLevel::Warning, core::LogCategory::General,
                  std::format("Picture {}: {}; shown at 1x", picture, scene.error().message));
    }
    cache_.push_front(std::move(entry));
    if (cache_.size() > kept) {
        cache_.pop_back();
    }
    const auto& image = cache_.front().image;
    return image ? &*image : nullptr;
}

} // namespace sci::app
