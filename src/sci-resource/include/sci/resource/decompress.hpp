// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 resource compression methods, ported from ScummVM.
//
// ScummVM's decompressors read from the open volume file rather than from a
// buffer of exactly the packed size: their bit readers fetch whole bytes
// ahead of need, and reads past the end of the file give zeros. These
// functions take the bytes from the start of the packed data to the end of
// the volume file and read them the same way, so that the results, including
// the success or failure of a decode, match the reference.

#pragma once

#include "sci/core/error.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::resource {

enum class Compression : std::int8_t {
    None,    // Method 0: stored.
    Lzw,     // Method 1 in SCI0: LZW, codes read least significant bit first.
    Huffman, // Method 2 in SCI0: Huffman, used for vector pictures.
};

// Decodes one resource. `input` starts at the packed data and runs to the
// end of the volume file; `packed_size` and `unpacked_size` come from the
// volume header. The result has exactly `unpacked_size` bytes.
[[nodiscard]] core::Result<std::vector<std::uint8_t>> decompress(Compression method,
                                                                 std::span<const std::uint8_t> input,
                                                                 std::uint32_t packed_size,
                                                                 std::uint32_t unpacked_size);

} // namespace sci::resource
