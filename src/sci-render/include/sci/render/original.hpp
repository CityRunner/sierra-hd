// SPDX-License-Identifier: GPL-3.0-or-later
//
// Original mode (spec section 6.1): the EGA picture with its dithering,
// without vector enhancements, every 1x pixel scaled to an N x N block. At
// N = 1 the frame is path 1's visual map itself.
#pragma once

#include "sci/pic/picture_maps.hpp"
#include "sci/render/rasterize.hpp"

#include <cstdint>
#include <vector>

namespace sci::render {

// The picture area of the visual map at 320N x 190N, as EGA colour indices.
[[nodiscard]] std::vector<std::uint8_t> render_original(const pic::PictureMaps& maps, int scale);

// EGA colour indices (width x height) as an RGBA image.
[[nodiscard]] RgbaImage indices_to_rgba(const std::vector<std::uint8_t>& indices, int width, int height);

} // namespace sci::render
