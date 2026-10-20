// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs the SCI0 decompressors on arbitrary data. Input: method (1 byte,
// modulo 3), packed size and unpacked size (16-bit little-endian each, as in
// a volume header), then the data up to the "end of the volume".

#include "sci/core/assert.hpp"
#include "sci/resource/decompress.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    if (input.size() < 5) {
        return 0;
    }
    const auto method = static_cast<sci::resource::Compression>(input[0] % 3);
    const std::uint32_t packed_size = std::uint32_t{input[1]} | (std::uint32_t{input[2]} << 8U);
    const std::uint32_t unpacked_size = std::uint32_t{input[3]} | (std::uint32_t{input[4]} << 8U);

    const auto result = sci::resource::decompress(method, input.subspan(5), packed_size, unpacked_size);
    SCI_ASSERT(!result || result->size() == unpacked_size);
    return 0;
}
