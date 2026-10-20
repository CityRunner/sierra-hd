// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/view.cpp (GfxView::initData for kViewEga,
//       unpackCelData for kViewEga, GfxView::getBitmap)
// and translated into this project's style, types and error handling.
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#include "sci/view/view.hpp"

#include "sci/core/byte_reader.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace sci::view {

namespace {

using core::ErrorCode;
using core::make_error;
using core::Result;

Result<std::uint16_t> u16_at(std::span<const std::uint8_t> data, std::size_t offset) {
    core::ByteReader reader(data);
    if (auto sought = reader.seek(offset); !sought) {
        return std::unexpected(sought.error());
    }
    return reader.read_u16le();
}

Result<std::uint8_t> u8_at(std::span<const std::uint8_t> data, std::size_t offset) {
    if (offset >= data.size()) {
        return make_error(ErrorCode::InvalidData, std::format("view offset {} is beyond its end", offset));
    }
    return data[offset];
}

// unpackCelData for EGA: each byte is a run, its high nibble the length and
// its low nibble the colour. The bitmap starts as colour 0.
Result<std::vector<std::uint8_t>> unpack_ega_cel(std::span<const std::uint8_t> data, std::size_t offset,
                                                 std::size_t pixel_count) {
    // A run covers at most 15 pixels. Checking this first keeps a corrupt
    // size (up to 65535 x 65535) from allocating gigabytes; ScummVM would
    // allocate it and then read past the resource.
    constexpr std::size_t max_run = 15;
    if (offset > data.size() || pixel_count > max_run * (data.size() - offset)) {
        return make_error(ErrorCode::InvalidData, "view cel is larger than its data can describe");
    }
    std::vector<std::uint8_t> pixels(pixel_count, 0);
    std::size_t pixel = 0;
    while (pixel < pixel_count) {
        if (offset >= data.size()) {
            return make_error(ErrorCode::InvalidData, "view cel data runs past the end of the resource");
        }
        const std::uint8_t run = data[offset++];
        const std::size_t length = run >> 4U;
        std::fill_n(pixels.begin() + static_cast<std::ptrdiff_t>(pixel), std::min(length, pixel_count - pixel),
                    static_cast<std::uint8_t>(run & 0x0FU));
        pixel += length;
    }
    return pixels;
}

// Cel header: width and height (words), displacement x (signed byte) and y
// (byte), the transparent colour, then the EGA run-length data.
Result<Cel> decode_cel(std::span<const std::uint8_t> data, std::size_t offset, bool mirrored) {
    const auto width = u16_at(data, offset);
    const auto height = u16_at(data, offset + 2);
    const auto displace_x = u8_at(data, offset + 4);
    const auto displace_y = u8_at(data, offset + 5);
    const auto clear_key = u8_at(data, offset + 6);
    if (!width || !height || !displace_x || !displace_y || !clear_key) {
        return make_error(ErrorCode::InvalidData, std::format("view cel header at {} is truncated", offset));
    }
    Cel cel{
        .width = *width,
        .height = *height,
        .displace_x = static_cast<std::int8_t>(*displace_x),
        .displace_y = *displace_y,
        .clear_key = *clear_key,
        .pixels = {},
    };
    auto pixels = unpack_ega_cel(data, offset + 7, std::size_t{cel.width} * cel.height);
    if (!pixels) {
        return std::unexpected(pixels.error());
    }
    cel.pixels = std::move(*pixels);
    if (mirrored) {
        cel.displace_x = static_cast<std::int16_t>(-cel.displace_x);
        for (std::size_t row = 0; row < cel.height; ++row) {
            const auto begin = cel.pixels.begin() + static_cast<std::ptrdiff_t>(row * cel.width);
            std::reverse(begin, begin + cel.width);
        }
    }
    return cel;
}

// Loop header: cel count (word), an unused word, then a word offset per cel.
Result<Loop> decode_loop(std::span<const std::uint8_t> data, std::size_t offset, bool mirrored) {
    const auto cel_count = u16_at(data, offset);
    if (!cel_count) {
        return std::unexpected(cel_count.error());
    }
    Loop loop{.mirrored = mirrored, .cels = {}};
    loop.cels.reserve(*cel_count);
    for (std::size_t i = 0; i < *cel_count; ++i) {
        const auto cel_offset = u16_at(data, offset + 4 + (2 * i));
        if (!cel_offset) {
            return std::unexpected(cel_offset.error());
        }
        auto cel = decode_cel(data, *cel_offset, mirrored);
        if (!cel) {
            return std::unexpected(cel.error());
        }
        loop.cels.push_back(std::move(*cel));
    }
    return loop;
}

} // namespace

// View header: loop count (byte), flags (byte), mirror mask (word, one bit
// per loop from bit 0), version (word), palette offset (word, unused for
// SCI0 EGA), then a word offset per loop.
Result<View> decode_view(std::span<const std::uint8_t> resource) {
    const auto loop_count = u8_at(resource, 0);
    const auto flags = u8_at(resource, 1);
    const auto mirror_mask = u16_at(resource, 2);
    const auto version = u16_at(resource, 4);
    if (!loop_count || !flags || !mirror_mask || !version) {
        return make_error(ErrorCode::InvalidData, "view header is truncated");
    }
    // ScummVM switches to the SCI1 or SCI1.1 view format on these markers,
    // even in EGA games.
    if (*flags == 0x80 || *version == 1) {
        return make_error(ErrorCode::Unsupported, "SCI1 and SCI1.1 views are not supported");
    }
    View view;
    view.loops.reserve(*loop_count);
    std::uint16_t mirror_bits = *mirror_mask;
    for (std::size_t i = 0; i < *loop_count; ++i) {
        const auto loop_offset = u16_at(resource, 8 + (2 * i));
        if (!loop_offset) {
            return std::unexpected(loop_offset.error());
        }
        auto loop = decode_loop(resource, *loop_offset, (mirror_bits & 1U) != 0);
        if (!loop) {
            return std::unexpected(loop.error());
        }
        view.loops.push_back(std::move(*loop));
        mirror_bits = static_cast<std::uint16_t>(mirror_bits >> 1U);
    }
    return view;
}

} // namespace sci::view
