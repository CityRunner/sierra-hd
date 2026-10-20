// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/resource/resource.cpp (ResourceManager::detectVolVersion,
//       Resource::readResourceInfo)
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

#include "sci/resource/volume.hpp"

#include "sci/core/byte_reader.hpp"

#include <algorithm>
#include <format>

namespace sci::resource {

namespace {

using core::ErrorCode;
using core::make_error;

// ScummVM checks at most the first megabyte of a volume.
constexpr std::size_t format_check_limit = 0x100000;

struct RawEntryHeader {
    std::uint16_t id = 0;
    std::uint16_t packed = 0; // Including the four header bytes after the id.
    std::uint16_t unpacked = 0;
    std::uint16_t method = 0;
};

core::Result<RawEntryHeader> read_raw_header(core::ByteReader& reader) {
    RawEntryHeader header;
    for (std::uint16_t* field : {&header.id, &header.packed, &header.unpacked, &header.method}) {
        const auto value = reader.read_u16le();
        if (!value) {
            return std::unexpected(value.error());
        }
        *field = *value;
    }
    return header;
}

// The plausibility test of detectVolVersion for SCI0 entries. The
// subtraction wraps as in ScummVM (uint32_t), so a packed size below 4 fails.
bool plausible_sci0_header(const RawEntryHeader& header) noexcept {
    const std::uint32_t packed = header.packed;
    const std::uint32_t unpacked = header.unpacked;
    return header.method <= 4 && (header.method != 0 || packed == unpacked + 4) && unpacked >= packed - 4U;
}

} // namespace

core::Result<VolumeEntryHeader> read_volume_entry_header(std::span<const std::uint8_t> volume, std::size_t offset) {
    if (volume.empty()) {
        return make_error(ErrorCode::InvalidData, "resource volume is empty");
    }
    core::ByteReader reader(volume);
    if (auto seek = reader.seek(offset); !seek) {
        return std::unexpected(seek.error());
    }
    const auto raw = read_raw_header(reader);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    // ScummVM subtracts the four header bytes without a check; a smaller value
    // would wrap to an impossible size, so it is rejected here.
    if (raw->packed < 4) {
        return make_error(ErrorCode::InvalidData,
                          std::format("resource at offset {} has a packed size field of {}", offset, raw->packed));
    }
    return VolumeEntryHeader{
        .id = resource_id_from_word(raw->id),
        .packed_size = raw->packed - 4U,
        .unpacked_size = raw->unpacked,
        .method = raw->method,
    };
}

core::Result<Compression> sci0_compression(std::uint16_t method) {
    switch (method) {
    case 0:
        return Compression::None;
    case 1:
        return Compression::Lzw;
    case 2:
        return Compression::Huffman;
    case 3:
    case 4:
    case 18:
    case 19:
    case 20:
        return make_error(ErrorCode::Unsupported,
                          std::format("compression method {} is not used by SCI0 games", method));
    default:
        return make_error(ErrorCode::InvalidData, std::format("unknown compression method {}", method));
    }
}

core::Result<void> check_sci0_volume(std::span<const std::uint8_t> volume) {
    // Headers are checked while they start within the first megabyte; a
    // header straddling that limit is still read in full.
    const std::size_t limit = std::min(volume.size(), format_check_limit);
    core::ByteReader reader(volume);
    while (reader.position() < limit) {
        // A header cut off by the end of the file ends the check successfully,
        // as the end of the stream does in ScummVM.
        const auto header = read_raw_header(reader);
        if (!header) {
            return {};
        }
        if (!plausible_sci0_header(*header)) {
            return make_error(ErrorCode::Unsupported,
                              std::format("resource volume is not in the SCI0 format (entry at offset {})",
                                          reader.position() - volume_entry_header_size));
        }
        // Skipping past the end of the volume also ends the check.
        if (!reader.skip(header->packed - 4U)) {
            return {};
        }
    }
    return {};
}

} // namespace sci::resource
