// SPDX-License-Identifier: GPL-3.0-or-later

#include "hd_pictures.hpp"

#include "sci/core/log.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/platform/picture_hints.hpp"
#include "sci/vector/build_scene.hpp"

#include <algorithm>
#include <array>
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
        return cache_.front().image ? &*cache_.front().image : nullptr;
    }
    Entry entry{.picture = picture, .mirrored = mirrored, .ega_palette = ega_palette, .scale = scale, .image = {}};
    const auto number = static_cast<std::uint16_t>(picture);
    const auto resource = manager_->load(resource::ResourceId{.type = resource::ResourceType::Pic, .number = number});
    pic::PictureMaps maps;
    pic::DrawTrace trace;
    const pic::DrawOptions options{
        .mirrored = mirrored, .add_to = false, .ega_palette = static_cast<std::uint8_t>(ega_palette)};
    const auto commands = resource ? pic::parse_pic(resource->data)
                                   : core::Result<std::vector<pic::PicCommand>>(std::unexpected(resource.error()));
    const auto drawn = commands ? pic::draw_picture(*commands, maps, options, &trace)
                                : core::Result<pic::DrawResult>(std::unexpected(commands.error()));
    if (drawn) {
        const std::array numbers{number};
        // The hints describe the picture as drawn unmirrored.
        const platform::PictureHintSet hints(mirrored ? std::nullopt : hints_, *manager_, numbers);
        const auto scene = vector::build_scene(maps, trace, vector::Plane::Visual, hints.pointers());
        entry.image = render::render_hd(scene.scene, scale, *executor_);
    } else {
        core::log(core::LogLevel::Warning, core::LogCategory::General,
                  std::format("Picture {}: {}; shown at 1x", picture, drawn.error().message));
    }
    cache_.push_front(std::move(entry));
    if (cache_.size() > kept) {
        cache_.pop_back();
    }
    return cache_.front().image ? &*cache_.front().image : nullptr;
}

} // namespace sci::app
