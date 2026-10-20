// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/resource/resource.cpp (ResourceManager::detectMapVersion,
//       ResourceManager::readResourceMapSCI0)
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

#include "sci/resource/resource_map.hpp"

#include "sci/core/byte_reader.hpp"

#include <algorithm>
#include <cstddef>
#include <format>

namespace sci::resource {

namespace {

using core::ErrorCode;
using core::make_error;

constexpr std::size_t entry_size = 6;
constexpr std::uint32_t terminator = 0xFFFFFFFFU;

constexpr std::uint8_t volume_of(std::uint32_t location) noexcept {
    return static_cast<std::uint8_t>(location >> 26U);
}

constexpr std::uint32_t offset_of(std::uint32_t location) noexcept {
    return location & 0x03FFFFFFU;
}

} // namespace

core::Result<void> check_sci0_map(std::span<const std::uint8_t> map, std::span<const std::uint8_t> volumes) {
    if (map.size() < 7) {
        return make_error(ErrorCode::InvalidData, std::format("resource map of {} bytes is too small", map.size()));
    }

    // SCI0 and SCI01 maps end with an entry whose location is 0xFFFFFFFF.
    const std::span<const std::uint8_t> tail = map.last(7);
    if (!std::ranges::all_of(tail.last(4), [](std::uint8_t byte) { return byte == 0xFF; })) {
        return make_error(ErrorCode::Unsupported, "resource map is not in the SCI0 format (SCI1 or later)");
    }
    // Seven 0xFF bytes at the end mark the 7-byte entries of KQ5 FM-Towns.
    if (tail[0] == 0xFF && tail[1] == 0xFF && tail[2] == 0xFF) {
        return make_error(ErrorCode::Unsupported, "resource map is in the KQ5 FM-Towns format");
    }

    // An entry naming a volume that does not exist means the SCI01 layout,
    // which uses fewer bits for the volume number. As in ScummVM, this scan
    // stops at the first entry starting with three 0xFF bytes.
    for (std::size_t position = 0; position + entry_size <= map.size(); position += entry_size) {
        const std::span<const std::uint8_t> entry = map.subspan(position, entry_size);
        if (entry[0] == 0xFF && entry[1] == 0xFF && entry[2] == 0xFF) {
            break;
        }
        const auto volume = static_cast<std::uint8_t>((entry[5] & 0xFCU) >> 2U);
        if (std::ranges::find(volumes, volume) == volumes.end()) {
            return make_error(
                ErrorCode::Unsupported,
                std::format("resource map names volume {}, which does not exist (SCI01 layout?)", volume));
        }
    }
    return {};
}

core::Result<std::vector<MapEntry>> parse_sci0_map(std::span<const std::uint8_t> map) {
    std::vector<MapEntry> entries;
    core::ByteReader reader(map);
    while (true) {
        const auto id = reader.read_u16le();
        const auto location = reader.read_u32le();
        if (!id || !location) {
            return make_error(ErrorCode::InvalidData, "resource map ends without a terminator");
        }
        if (*location == terminator) {
            return entries;
        }
        entries.push_back(MapEntry{
            .id = resource_id_from_word(*id),
            .volume = volume_of(*location),
            .offset = offset_of(*location),
        });
    }
}

} // namespace sci::resource
