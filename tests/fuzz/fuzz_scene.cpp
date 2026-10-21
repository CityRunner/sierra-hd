// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads arbitrary bytes as a cached vector scene: the reader must return a
// scene or an error, never fail otherwise, and a scene it accepts must
// write back to the same bytes.

#include "sci/core/assert.hpp"
#include "sci/vector/scene_io.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes(data, size);
    if (const auto scene = sci::vector::read_scene(bytes)) {
        const auto written = sci::vector::write_scene(*scene);
        SCI_ASSERT(std::ranges::equal(written, bytes));
    }
    return 0;
}
