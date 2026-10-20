// SPDX-License-Identifier: GPL-3.0-or-later

#include "png.hpp"

#include "game_folder.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>

namespace sci::tools {

namespace {

constexpr Rgb rgb(std::uint32_t hex) noexcept {
    return Rgb{
        .r = static_cast<std::uint8_t>((hex >> 16U) & 0xFFU),
        .g = static_cast<std::uint8_t>((hex >> 8U) & 0xFFU),
        .b = static_cast<std::uint8_t>(hex & 0xFFU),
    };
}

constexpr std::array<Rgb, 16> ega_palette{
    rgb(0x000000), rgb(0x0000AA), rgb(0x00AA00), rgb(0x00AAAA), rgb(0xAA0000), rgb(0xAA00AA),
    rgb(0xAA5500), rgb(0xAAAAAA), rgb(0x555555), rgb(0x5555FF), rgb(0x55FF55), rgb(0x55FFFF),
    rgb(0xFF5555), rgb(0xFF55FF), rgb(0xFFFF55), rgb(0xFFFFFF),
};

std::uint32_t crc32(std::span<const std::uint8_t> bytes, std::uint32_t crc = 0) {
    crc = ~crc;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

void put_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xFFU));
    }
}

void put_chunk(std::vector<std::uint8_t>& out, std::string_view type, std::span<const std::uint8_t> data) {
    put_u32_be(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type.begin(), type.end());
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put_u32_be(out, crc32(body));
}

// A zlib stream of stored (uncompressed) deflate blocks.
std::vector<std::uint8_t> zlib_stored(std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> out{0x78, 0x01};
    constexpr std::size_t max_block = 65535;
    std::size_t position = 0;
    do {
        const std::size_t length = std::min(max_block, data.size() - position);
        const bool last = position + length == data.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(length & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(length >> 8U));
        out.push_back(static_cast<std::uint8_t>(~length & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((~length >> 8U) & 0xFFU));
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(position),
                   data.begin() + static_cast<std::ptrdiff_t>(position + length));
        position += length;
    } while (position < data.size());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : data) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    put_u32_be(out, (b << 16U) | a);
    return out;
}

} // namespace

Image::Image(int width, int height, Rgb background)
    : width_(width), height_(height),
      pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), background) {
}

void Image::set(int x, int y, Rgb color) {
    if (x >= 0 && y >= 0 && x < width_ && y < height_) {
        pixels_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)) + static_cast<std::size_t>(x)] = color;
    }
}

bool Image::write_png(const std::filesystem::path& path) const {
    std::vector<std::uint8_t> raw;
    raw.reserve((pixels_.size() * 3) + static_cast<std::size_t>(height_));
    for (int y = 0; y < height_; ++y) {
        raw.push_back(0); // Filter type: none.
        for (int x = 0; x < width_; ++x) {
            const Rgb& pixel =
                pixels_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)) + static_cast<std::size_t>(x)];
            raw.insert(raw.end(), {pixel.r, pixel.g, pixel.b});
        }
    }
    std::vector<std::uint8_t> header;
    put_u32_be(header, static_cast<std::uint32_t>(width_));
    put_u32_be(header, static_cast<std::uint32_t>(height_));
    header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit RGB, no interlace.

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    put_chunk(png, "IHDR", header);
    put_chunk(png, "IDAT", zlib_stored(raw));
    put_chunk(png, "IEND", {});
    return write_file(path, png);
}

Rgb ega_color(std::uint8_t index) noexcept {
    return ega_palette[index & 0x0FU];
}

} // namespace sci::tools
