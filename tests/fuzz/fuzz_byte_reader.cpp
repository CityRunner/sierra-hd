// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives ByteReader with operations taken from the input itself and checks
// that it never reads outside the data and that failed reads do not move it.

#include "sci/core/assert.hpp"
#include "sci/core/byte_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    sci::core::ByteReader reader(input);

    // Each step consumes at least the operation byte, so the loop ends.
    while (const auto operation = reader.read_u8()) {
        const std::size_t before = reader.position();
        bool succeeded = false;
        switch (*operation % 6U) {
        case 0:
            succeeded = reader.read_u8().has_value();
            break;
        case 1:
            succeeded = reader.read_u16le().has_value();
            break;
        case 2:
            succeeded = reader.read_u32le().has_value();
            break;
        case 3:
            succeeded = reader.read_bytes(*operation / 6U).has_value();
            break;
        case 4:
            // Forward seeks only, so every step advances.
            succeeded = reader.seek(before + (*operation / 6U)).has_value();
            break;
        default:
            succeeded = reader.skip(*operation / 6U).has_value();
            break;
        }
        SCI_ASSERT(reader.position() <= reader.size());
        SCI_ASSERT(succeeded || reader.position() == before);
    }
    SCI_ASSERT(reader.at_end());
    return 0;
}
