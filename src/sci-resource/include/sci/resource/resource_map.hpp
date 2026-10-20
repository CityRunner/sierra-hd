// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 resource map (RESOURCE.MAP): six-byte entries of a 16-bit id
// (type in the top 5 bits, number in the low 11) and a 32-bit location
// (volume number in the top 6 bits, offset in the low 26), ended by an entry
// whose location is 0xFFFFFFFF.

#pragma once

#include "sci/core/error.hpp"
#include "sci/resource/resource_id.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::resource {

struct MapEntry {
    ResourceId id;
    std::uint8_t volume = 0;
    std::uint32_t offset = 0;
};

// Checks that the map is in the SCI0 format and that every volume it names
// is among `volumes` (ScummVM's detectMapVersion). Maps of later SCI
// versions, KQ5 FM-Towns maps and SCI01 maps are Unsupported.
[[nodiscard]] core::Result<void> check_sci0_map(std::span<const std::uint8_t> map,
                                                std::span<const std::uint8_t> volumes);

// All entries up to the terminator, in file order, duplicates included
// (ScummVM's readResourceMapSCI0). A map without a terminator is InvalidData.
[[nodiscard]] core::Result<std::vector<MapEntry>> parse_sci0_map(std::span<const std::uint8_t> map);

} // namespace sci::resource
