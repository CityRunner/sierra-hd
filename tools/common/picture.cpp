// SPDX-License-Identifier: GPL-3.0-or-later

#include "picture.hpp"

#include "sci/pic/parse_pic.hpp"

#include <print>

namespace sci::tools {

std::vector<std::uint16_t> numbers_of(const resource::ResourceManager& manager, resource::ResourceType type) {
    std::vector<std::uint16_t> numbers;
    for (const resource::ResourceId id : manager.list()) {
        if (id.type == type) {
            numbers.push_back(id.number);
        }
    }
    return numbers;
}

std::optional<DrawnPicture> draw_pictures(const resource::ResourceManager& manager,
                                          std::span<const std::uint16_t> numbers, pic::DrawOptions options) {
    DrawnPicture drawn;
    for (std::size_t i = 0; i < numbers.size(); ++i) {
        const auto resource =
            manager.load(resource::ResourceId{.type = resource::ResourceType::Pic, .number = numbers[i]});
        if (!resource) {
            std::println(stderr, "pic.{}: {}", numbers[i], resource.error().message);
            return std::nullopt;
        }
        const auto commands = pic::parse_pic(resource->data);
        options.add_to = i > 0;
        const auto result = commands ? pic::draw_picture(*commands, drawn.maps, options, &drawn.trace)
                                     : core::Result<pic::DrawResult>(std::unexpected(commands.error()));
        if (!result) {
            std::println(stderr, "pic.{}: {}", numbers[i], result.error().message);
            return std::nullopt;
        }
    }
    return drawn;
}

Image to_image(const render::RgbaImage& frame) {
    Image image(frame.width, frame.height);
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const std::size_t i =
                ((static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width)) + static_cast<std::size_t>(x)) *
                4;
            image.set(x, y, {.r = frame.pixels[i], .g = frame.pixels[i + 1], .b = frame.pixels[i + 2]});
        }
    }
    return image;
}

} // namespace sci::tools
