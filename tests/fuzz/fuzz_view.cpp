// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decodes arbitrary view, font and cursor data, checking the decoded sizes.

#include "sci/core/assert.hpp"
#include "sci/text/font.hpp"
#include "sci/view/cursor.hpp"
#include "sci/view/view.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> input(data, size);
    if (const auto view = sci::view::decode_view(input)) {
        for (const sci::view::Loop& loop : view->loops) {
            for (const sci::view::Cel& cel : loop.cels) {
                SCI_ASSERT(cel.pixels.size() == std::size_t{cel.width} * cel.height);
            }
        }
    }
    if (const auto font = sci::text::decode_font(input)) {
        for (const sci::text::Glyph& glyph : font->glyphs) {
            SCI_ASSERT(glyph.pixels.size() == std::size_t{glyph.width} * glyph.height);
        }
    }
    if (const auto cursor = sci::view::decode_cursor(input)) {
        SCI_ASSERT(size == 68 && cursor->hotspot_x == cursor->hotspot_y && cursor->hotspot_x % 8 == 0);
    }
    return 0;
}
