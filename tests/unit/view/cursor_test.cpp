// SPDX-License-Identifier: GPL-3.0-or-later
//
// SCI0 cursor resources (sci/view/cursor.hpp), as GfxCursor::kernelSetShape
// in engines/sci/graphics/cursor.cpp decodes them.

#include "sci/view/cursor.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

using sci::view::CursorPixel;
using sci::view::decode_cursor;

namespace {

// A cursor resource: 4 header bytes, then 16 words of mask A (transparency)
// and 16 of mask B (colour), little endian, the leftmost pixel in bit 15.
std::array<std::uint8_t, 68> cursor_resource(std::uint8_t hotspot_flag) {
    std::array<std::uint8_t, 68> data{};
    data[3] = hotspot_flag;
    const auto set_row = [&](std::size_t mask, std::size_t y, std::uint16_t bits) {
        const std::size_t at = 4 + (mask * 32) + (y * 2);
        data[at] = static_cast<std::uint8_t>(bits & 0xFFU);
        data[at + 1] = static_cast<std::uint8_t>(bits >> 8U);
    };
    // Row 0: A+B (white), B (white), neither (black), A (transparent).
    set_row(0, 0, 0x9000);
    set_row(1, 0, 0xC000);
    // Row 1 is all transparent, row 2 has a white pixel at the right.
    set_row(0, 1, 0xFFFF);
    set_row(0, 2, 0xFFFE);
    set_row(1, 2, 0x0001);
    return data;
}

} // namespace

SCI_TEST(cursor_masks_give_black_white_and_transparent) {
    const auto data = cursor_resource(0);
    const auto cursor = decode_cursor(data);
    SCI_CHECK(cursor.has_value());
    if (!cursor) {
        return;
    }
    SCI_CHECK(cursor->at(0, 0) == CursorPixel::White); // Colour 3 is white in SCI0.
    SCI_CHECK(cursor->at(1, 0) == CursorPixel::White); // Colour 1.
    SCI_CHECK(cursor->at(2, 0) == CursorPixel::Black); // Colour 0.
    SCI_CHECK(cursor->at(3, 0) == CursorPixel::Transparent);
    SCI_CHECK(cursor->at(15, 0) == CursorPixel::Black);
    for (int x = 0; x < 16; ++x) {
        SCI_CHECK(cursor->at(x, 1) == CursorPixel::Transparent);
    }
    SCI_CHECK(cursor->at(0, 2) == CursorPixel::Transparent && cursor->at(14, 2) == CursorPixel::Transparent);
    SCI_CHECK(cursor->at(15, 2) == CursorPixel::White);
    SCI_CHECK(cursor->at(7, 15) == CursorPixel::Black);
}

SCI_TEST(cursor_hotspot_is_centred_when_byte_3_is_set) {
    const auto top_left = decode_cursor(cursor_resource(0));
    SCI_CHECK(top_left && top_left->hotspot_x == 0 && top_left->hotspot_y == 0);
    const auto centred = decode_cursor(cursor_resource(1));
    SCI_CHECK(centred && centred->hotspot_x == 8 && centred->hotspot_y == 8);
    // kernelSetShape tests the whole byte, not only bit 0.
    const auto other = decode_cursor(cursor_resource(2));
    SCI_CHECK(other && other->hotspot_x == 8 && other->hotspot_y == 8);
}

SCI_TEST(cursor_resources_of_another_size_are_refused) {
    const std::array<std::uint8_t, 67> short_data{};
    SCI_CHECK(!decode_cursor(short_data).has_value());
    const std::array<std::uint8_t, 69> long_data{};
    SCI_CHECK(!decode_cursor(long_data).has_value());
    SCI_CHECK(!decode_cursor({}).has_value());
}
