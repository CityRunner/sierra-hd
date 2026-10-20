// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/scifont.cpp (GfxFontFromResource constructor,
//       getCharData, drawToBuffer)
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

#include "sci/text/font.hpp"

#include "sci/core/byte_reader.hpp"

#include <cstddef>
#include <format>
#include <utility>

namespace sci::text {

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

// The glyph whose header (width, height) is at `offset`, rendered as
// drawToBuffer does: rows of (width + 7) / 8 bytes, most significant bit
// first.
Result<Glyph> decode_glyph(std::span<const std::uint8_t> data, std::size_t offset, std::size_t character) {
    // ScummVM keeps the offset in an int16; larger offsets do not work there.
    if (offset >= 0x8000) {
        return make_error(ErrorCode::InvalidData, std::format("font glyph {} has offset {}", character, offset));
    }
    // An offset without room for the header gives an empty glyph (a
    // workaround in ScummVM for broken fonts of some fan translations).
    if (offset + 2 > data.size()) {
        return Glyph{};
    }
    Glyph glyph{.width = data[offset], .height = data[offset + 1], .pixels = {}};
    const std::size_t bytes_per_row = (std::size_t{glyph.width} + 7) / 8;
    const std::size_t bitmap_offset = offset + 2;
    if (bytes_per_row * glyph.height > data.size() - bitmap_offset) {
        return make_error(ErrorCode::InvalidData,
                          std::format("font glyph {} bitmap runs past the end of the resource", character));
    }
    glyph.pixels.assign(std::size_t{glyph.width} * glyph.height, 0);
    for (std::size_t y = 0; y < glyph.height; ++y) {
        const std::span<const std::uint8_t> row = data.subspan(bitmap_offset + (y * bytes_per_row), bytes_per_row);
        for (std::size_t x = 0; x < glyph.width; ++x) {
            if (((static_cast<unsigned>(row[x / 8]) << (x % 8)) & 0x80U) != 0) {
                glyph.pixels[(y * glyph.width) + x] = 1;
            }
        }
    }
    return glyph;
}

} // namespace

// Header: an unused word, the character count (word), the line height
// (word; ScummVM keeps its low byte), then a word offset per character.
Result<Font> decode_font(std::span<const std::uint8_t> resource) {
    const auto count = u16_at(resource, 2);
    const auto height = u16_at(resource, 4);
    if (!count || !height) {
        return make_error(ErrorCode::InvalidData, "font header is truncated");
    }
    Font font{.line_height = static_cast<std::uint8_t>(*height & 0xFFU), .glyphs = {}};
    font.glyphs.reserve(*count);
    for (std::size_t character = 0; character < *count; ++character) {
        const auto offset = u16_at(resource, 6 + (2 * character));
        if (!offset) {
            return make_error(ErrorCode::InvalidData, "font character table is truncated");
        }
        auto glyph = decode_glyph(resource, *offset, character);
        if (!glyph) {
            return std::unexpected(glyph.error());
        }
        font.glyphs.push_back(std::move(*glyph));
    }
    return font;
}

} // namespace sci::text
