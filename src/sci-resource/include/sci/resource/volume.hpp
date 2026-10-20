// SPDX-License-Identifier: GPL-3.0-or-later
//
// SCI0 resource volumes (RESOURCE.001 ...): each resource is an eight-byte
// header (id, packed size + 4, unpacked size, compression method) followed by
// the packed data.

#pragma once

#include "sci/core/error.hpp"
#include "sci/resource/decompress.hpp"
#include "sci/resource/resource_id.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sci::resource {

inline constexpr std::size_t volume_entry_header_size = 8;

struct VolumeEntryHeader {
    ResourceId id;
    std::uint32_t packed_size = 0; // Without the four header bytes that the stored value includes.
    std::uint32_t unpacked_size = 0;
    std::uint16_t method = 0;
};

// Reads the header of the resource at `offset` (ScummVM's readResourceInfo
// for SCI0 volumes).
[[nodiscard]] core::Result<VolumeEntryHeader> read_volume_entry_header(std::span<const std::uint8_t> volume,
                                                                       std::size_t offset);

// The compression of an SCI0 method number. Methods that only later SCI
// versions use (3, 4, 18-20) are Unsupported; others are InvalidData.
[[nodiscard]] core::Result<Compression> sci0_compression(std::uint16_t method);

// Checks that the volume's first megabyte parses as a chain of SCI0 entries
// (ScummVM's detectVolVersion, SCI0 case). Other formats are Unsupported.
[[nodiscard]] core::Result<void> check_sci0_volume(std::span<const std::uint8_t> volume);

} // namespace sci::resource
