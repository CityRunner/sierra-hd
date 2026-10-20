// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/text/font.hpp"
#include "sci_test.hpp"

#include <cstdint>
#include <vector>

using sci::core::ErrorCode;

namespace {

// Three characters: 0 is a 10x2 glyph (two bytes per row), 1 has an offset
// beyond the resource, 2 is empty (0x0).
std::vector<std::uint8_t> sample_font() {
    std::vector<std::uint8_t> font{0, 0, 3, 0, 9, 0};
    font.insert(font.end(), {12, 0, 0xFF, 0x00, 18, 0});
    font.insert(font.end(), {10, 2, 0xA0, 0x40, 0x01, 0x80});
    font.insert(font.end(), {0, 0});
    return font;
}

} // namespace

SCI_TEST(font_glyphs_decode_bits_msb_first) {
    const auto font = sci::text::decode_font(sample_font());
    SCI_CHECK(font.has_value() && font->glyphs.size() == 3 && font->line_height == 9);
    if (font && font->glyphs.size() == 3) {
        const sci::text::Glyph& glyph = font->glyphs[0];
        SCI_CHECK(glyph.width == 10 && glyph.height == 2);
        // Row 0: 0xA0 0x40 -> 1 0 1 0 0 0 0 0 | 0 1; row 1: 0x01 0x80 -> ... 1 | 1 0.
        const std::vector<std::uint8_t> expected{1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0};
        SCI_CHECK(glyph.pixels == expected);
        SCI_CHECK(font->glyphs[1].width == 0 && font->glyphs[1].pixels.empty());
        SCI_CHECK(font->glyphs[2].width == 0);
    }
}

SCI_TEST(font_bitmaps_must_fit_the_resource) {
    std::vector<std::uint8_t> font = sample_font();
    font.resize(font.size() - 3); // Cuts glyph 0's bitmap.
    const auto decoded = sci::text::decode_font(font);
    SCI_CHECK(!decoded && decoded.error().code == ErrorCode::InvalidData);
}
