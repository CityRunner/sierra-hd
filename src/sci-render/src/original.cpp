// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/render/original.hpp"

#include "sci/core/assert.hpp"
#include "sci/render/colors.hpp"
#include "sci/vector/scene.hpp"

#include <cstddef>

namespace sci::render {

std::vector<std::uint8_t> render_original(const pic::PictureMaps& maps, int scale) {
    SCI_ASSERT(scale >= 1);
    const auto n = static_cast<std::size_t>(scale);
    const std::size_t width = vector::picture_width * n;
    std::vector<std::uint8_t> frame(width * vector::picture_height * n);
    for (std::size_t y = 0; y < frame.size() / width; ++y) {
        const auto source_row = static_cast<int>(y / n) + pic::picture_top;
        for (std::size_t x = 0; x < width; ++x) {
            frame[(y * width) + x] = maps.visual[pic::PictureMaps::index(static_cast<int>(x / n), source_row)];
        }
    }
    return frame;
}

RgbaImage indices_to_rgba(const std::vector<std::uint8_t>& indices, int width, int height) {
    SCI_ASSERT(indices.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    RgbaImage image{.width = width, .height = height, .pixels = std::vector<std::uint8_t>(indices.size() * 4)};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const Rgb8 color = ega_palette[indices[i] & 0x0FU];
        image.pixels[(i * 4) + 0] = color.r;
        image.pixels[(i * 4) + 1] = color.g;
        image.pixels[(i * 4) + 2] = color.b;
        image.pixels[(i * 4) + 3] = 255;
    }
    return image;
}

} // namespace sci::render
