// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/graphics/cursor.cpp (GfxCursor::kernelSetShape, SCI0 cursors)
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

#include "sci/view/cursor.hpp"

#include <array>
#include <format>

namespace sci::view {

namespace {

constexpr std::size_t resource_size = 68; // SCI_CURSOR_SCI0_RESOURCESIZE

// The colours of the masks' bit pairs: 0 black, 2 transparent, 1 and 3
// white (the gray of SCI1 cursors is white in SCI0).
constexpr std::array<CursorPixel, 4> pixel_of{
    CursorPixel::Black,
    CursorPixel::White,
    CursorPixel::Transparent,
    CursorPixel::White,
};

} // namespace

core::Result<Cursor> decode_cursor(std::span<const std::uint8_t> resource) {
    if (resource.size() != resource_size) {
        return core::make_error(core::ErrorCode::InvalidData,
                                std::format("cursor resource has {} bytes, not {}", resource.size(), resource_size));
    }
    Cursor cursor;
    // SCI0 cursors hold a hotspot flag, not coordinates: centred if byte 3
    // is set, else the top left.
    cursor.hotspot_x = cursor.hotspot_y = resource[3] != 0 ? Cursor::size / 2 : 0;
    const auto word = [&](std::size_t at) {
        return static_cast<unsigned>(resource[at]) | (static_cast<unsigned>(resource[at + 1]) << 8U);
    };
    for (int y = 0; y < Cursor::size; ++y) {
        const unsigned mask_a = word(4 + (static_cast<std::size_t>(y) * 2));
        const unsigned mask_b = word(4 + 32 + (static_cast<std::size_t>(y) * 2));
        for (int x = 0; x < Cursor::size; ++x) {
            const unsigned color = (((mask_a << static_cast<unsigned>(x)) & 0x8000U) |
                                    (((mask_b << static_cast<unsigned>(x)) >> 1U) & 0x4000U)) >>
                                   14U;
            cursor.pixels[(static_cast<std::size_t>(y) * Cursor::size) + static_cast<std::size_t>(x)] =
                pixel_of.at(color);
        }
    }
    return cursor;
}

} // namespace sci::view
