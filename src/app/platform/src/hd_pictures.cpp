// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/hd_pictures.hpp"

#include "sci/core/log.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace sci::platform {

namespace {

constexpr std::size_t kept = 4;
constexpr int picture_top = 10; // The picture's first screen row.

} // namespace

SceneVariant stack_variant(std::span<const vm::ShownPicture> stack, vector::Plane plane) {
    SceneVariant variant{.picture = 0, .plane = plane, .mirrored = false, .ega_palette = 0, .overlays = {}};
    if (stack.empty()) {
        return variant;
    }
    variant.picture = static_cast<std::uint16_t>(stack.front().number);
    variant.mirrored = stack.front().mirrored;
    variant.ega_palette = static_cast<std::uint8_t>(stack.front().ega_palette);
    for (const vm::ShownPicture& overlay : stack.subspan(1)) {
        variant.overlays.push_back(PictureOverlay{
            .picture = static_cast<std::uint16_t>(overlay.number),
            .mirrored = overlay.mirrored,
            .ega_palette = static_cast<std::uint8_t>(overlay.ega_palette),
        });
    }
    return variant;
}

vector::Scene with_lines(const vector::Scene& visual, std::span<const vm::ShownLine> lines) {
    vector::Scene scene = visual;
    for (const vm::ShownLine& line : lines) {
        // Through the pixel centres, one pixel wide, in the solid pair of
        // the line's colour.
        const vector::Vec2 start{.x = line.x0 + 0.5, .y = line.y0 - picture_top + 0.5};
        const vector::Vec2 end{.x = line.x1 + 0.5, .y = line.y1 - picture_top + 0.5};
        const unsigned color = line.color & 0x0FU;
        vector::Stroke stroke{
            .source = -1,
            .points = {start},
            .radius = 0.5,
            .value = static_cast<std::uint8_t>(color | (color << 4U)),
            .fitted = false,
            .cuts = {},
        };
        if (end != start) {
            stroke.points.push_back(end);
        }
        scene.layers.emplace_back(std::move(stroke));
    }
    return scene;
}

HdImages HdPictures::get(std::span<const vm::ShownPicture> stack, std::span<const vm::ShownLine> lines, int scale) {
    if (stack.empty()) {
        return {};
    }
    const auto same = [&](const Entry& e) {
        return e.scale == scale && std::ranges::equal(e.stack, stack) && std::ranges::equal(e.lines, lines);
    };
    if (const auto it = std::ranges::find_if(cache_, same); it != cache_.end()) {
        cache_.splice(cache_.begin(), cache_, it);
    } else {
        Entry entry{
            .stack = {stack.begin(), stack.end()},
            .lines = {lines.begin(), lines.end()},
            .scale = scale,
            .background = {},
            .priority = {},
        };
        const auto visual = analysis_->scene(stack_variant(stack, vector::Plane::Visual));
        if (visual) {
            entry.background = lines.empty() ? render::render_hd(**visual, scale, *executor_)
                                             : render::render_hd(with_lines(**visual, lines), scale, *executor_);
        } else {
            core::log(core::LogLevel::Warning, core::LogCategory::General,
                      std::format("Picture {}: {}; shown at 1x", stack.front().number, visual.error().message));
        }
        const auto priority = analysis_->scene(stack_variant(stack, vector::Plane::Priority));
        if (priority) {
            entry.priority = render::render_priority(**priority, scale, *executor_);
        } else {
            core::log(core::LogLevel::Warning, core::LogCategory::General,
                      std::format("Picture {}: {}; priority at 1x", stack.front().number, priority.error().message));
        }
        cache_.push_front(std::move(entry));
        if (cache_.size() > kept) {
            cache_.pop_back();
        }
    }
    const Entry& front = cache_.front();
    return HdImages{
        .background = front.background ? &*front.background : nullptr,
        .priority = front.priority,
    };
}

} // namespace sci::platform
