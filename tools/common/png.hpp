// SPDX-License-Identifier: GPL-3.0-or-later
//
// A minimal PNG writer for development images (RGB, 8 bits per channel,
// stored deflate blocks, so no compression library is needed).

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace sci::tools {

struct Rgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

class Image {
public:
    Image(int width, int height, Rgb background = {});

    [[nodiscard]] int width() const noexcept { return width_; }

    [[nodiscard]] int height() const noexcept { return height_; }

    // Pixels outside the image are ignored.
    void set(int x, int y, Rgb color);

    [[nodiscard]] bool write_png(const std::filesystem::path& path) const;

private:
    int width_;
    int height_;
    std::vector<Rgb> pixels_;
};

// The 16 EGA colours as ScummVM shows them (GfxPalette::setEGA).
[[nodiscard]] Rgb ega_color(std::uint8_t index) noexcept;

} // namespace sci::tools
