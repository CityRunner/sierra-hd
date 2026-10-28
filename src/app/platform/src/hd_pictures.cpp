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

void HdPictures::make(Entry& entry, AnalysisCache& analysis, core::Executor& executor) {
    const auto visual = analysis.scene(stack_variant(entry.stack, vector::Plane::Visual));
    if (visual) {
        entry.background = entry.lines.empty()
                               ? render::render_hd(**visual, entry.scale, executor)
                               : render::render_hd(with_lines(**visual, entry.lines), entry.scale, executor);
    } else {
        core::log(core::LogLevel::Warning, core::LogCategory::General,
                  std::format("Picture {}: {}; shown at 1x", entry.stack.front().number, visual.error().message));
    }
    const auto priority = analysis.scene(stack_variant(entry.stack, vector::Plane::Priority));
    if (priority) {
        entry.priority = render::render_priority(**priority, entry.scale, executor);
    } else {
        core::log(core::LogLevel::Warning, core::LogCategory::General,
                  std::format("Picture {}: {}; priority at 1x", entry.stack.front().number, priority.error().message));
    }
}

const HdPictures::Entry* HdPictures::find(std::span<const vm::ShownPicture> stack, std::span<const vm::ShownLine> lines,
                                          int scale) {
    const auto same = [&](const Entry& e) {
        return e.scale == scale && std::ranges::equal(e.stack, stack) && std::ranges::equal(e.lines, lines);
    };
    const auto it = std::ranges::find_if(cache_, same);
    if (it == cache_.end()) {
        return nullptr;
    }
    cache_.splice(cache_.begin(), cache_, it);
    return &cache_.front();
}

const HdPictures::Entry& HdPictures::keep(Entry entry) {
    cache_.push_front(std::move(entry));
    if (cache_.size() > kept) {
        cache_.pop_back();
    }
    return cache_.front();
}

HdImages HdPictures::images(const Entry& entry) {
    return HdImages{
        .background = entry.background ? &*entry.background : nullptr,
        .priority = entry.priority,
        .pending = false,
    };
}

HdImages HdPictures::get(std::span<const vm::ShownPicture> stack, std::span<const vm::ShownLine> lines, int scale) {
    if (stack.empty()) {
        return {};
    }
    if (const Entry* entry = find(stack, lines, scale)) {
        return images(*entry);
    }
    Entry entry{
        .stack = {stack.begin(), stack.end()},
        .lines = {lines.begin(), lines.end()},
        .scale = scale,
        .background = {},
        .priority = {},
    };
    make(entry, *analysis_, *executor_);
    return images(keep(std::move(entry)));
}

HdImages HdPictures::request(std::span<const vm::ShownPicture> stack, std::span<const vm::ShownLine> lines, int scale) {
    if (stack.empty()) {
        return {};
    }
    if (auto done = task_.take()) {
        keep(std::move(*done));
    }
    if (const Entry* entry = find(stack, lines, scale)) {
        return images(*entry);
    }
    if (!task_.busy()) {
        // The thread gets its own copy of what it renders.
        task_.start([entry =
                         Entry{
                             .stack = {stack.begin(), stack.end()},
                             .lines = {lines.begin(), lines.end()},
                             .scale = scale,
                             .background = {},
                             .priority = {},
                         },
                     analysis = analysis_, executor = executor_] mutable {
            make(entry, *analysis, *executor);
            return std::move(entry);
        });
    }
    return HdImages{.background = nullptr, .priority = {}, .pending = true};
}

} // namespace sci::platform
