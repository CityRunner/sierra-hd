// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing pictures of a game folder through path 1, for the tools.
#pragma once

#include "png.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/draw_trace.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/resource/resource_manager.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sci::tools {

// The numbers of all resources of a type, ascending.
[[nodiscard]] std::vector<std::uint16_t> numbers_of(const resource::ResourceManager& manager,
                                                    resource::ResourceType type);

struct DrawnPicture {
    pic::PictureMaps maps;
    pic::DrawTrace trace;
};

// Draws the pictures in order, each further one over the previous ones (an
// overlay), with the trace. Errors go to stderr.
[[nodiscard]] std::optional<DrawnPicture> draw_pictures(const resource::ResourceManager& manager,
                                                        std::span<const std::uint16_t> numbers,
                                                        pic::DrawOptions options = {});

[[nodiscard]] Image to_image(const render::RgbaImage& frame);

} // namespace sci::tools
