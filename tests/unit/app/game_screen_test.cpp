// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window layer's input and cursor, made neutral of SDL: key presses as
// SCI key events (ScummVM's EventManager::getScummVMEvent, keyMappings and
// altify in engines/sci/event.cpp) and SCI0 cursor resources as RGBA images
// (GfxCursor::kernelSetShape in engines/sci/graphics/cursor.cpp).

#include "game_screen.hpp"
#include "sci/vm/events.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

using sci::app::CursorImage;
using sci::app::decode_cursor;
using sci::app::KeyPress;
using sci::app::sci_key_event;
using Key = sci::app::KeyPress::Key;

namespace {

// An IBM scancode in the high byte, as the SCI key codes have it.
constexpr int scan(unsigned scancode) {
    return static_cast<int>(scancode << 8U);
}

// The character of the key event, or -1 for none.
int code(const KeyPress& press) {
    const auto event = sci_key_event(press);
    if (!event) {
        return -1;
    }
    SCI_CHECK(event->type == sci::vm::event_key_down);
    return event->character;
}

KeyPress character(char c) {
    return KeyPress{.key = Key::Character, .character = c};
}

KeyPress function(int number, bool shift) {
    return KeyPress{.key = Key::Function, .function = number, .shift = shift};
}

KeyPress keypad(Key key, char c, bool num_lock) {
    return KeyPress{.key = key, .character = c, .keypad = true, .num_lock = num_lock};
}

} // namespace

SCI_TEST(characters_pass_through_with_their_modifiers) {
    SCI_CHECK(code(character('a')) == 'a');
    SCI_CHECK(code(character(' ')) == ' ');
    const auto shifted = sci_key_event(KeyPress{.key = Key::Character, .character = 'A', .shift = true});
    SCI_CHECK(shifted && shifted->character == 'A' && shifted->modifiers == sci::vm::modifier_shift);
    // The game's fonts are not extended, so no character from 0x80 on.
    SCI_CHECK(code(character('\xE9')) == -1);
    SCI_CHECK(code(KeyPress{.key = Key::Other}) == -1);
}

SCI_TEST(named_keys_are_ascii_or_ibm_scancodes) {
    SCI_CHECK(code(KeyPress{.key = Key::Enter}) == 13);
    SCI_CHECK(code(KeyPress{.key = Key::Escape}) == 27);
    SCI_CHECK(code(KeyPress{.key = Key::Backspace}) == 8);
    SCI_CHECK(code(KeyPress{.key = Key::Tab}) == '\t');
    SCI_CHECK(code(KeyPress{.key = Key::Tab, .shift = true}) == scan(15)); // kSciKeyShiftTab
    SCI_CHECK(code(KeyPress{.key = Key::Home}) == scan(71));
    SCI_CHECK(code(KeyPress{.key = Key::Up}) == scan(72));
    SCI_CHECK(code(KeyPress{.key = Key::PageUp}) == scan(73));
    SCI_CHECK(code(KeyPress{.key = Key::Left}) == scan(75));
    SCI_CHECK(code(KeyPress{.key = Key::Center}) == scan(76));
    SCI_CHECK(code(KeyPress{.key = Key::Right}) == scan(77));
    SCI_CHECK(code(KeyPress{.key = Key::End}) == scan(79));
    SCI_CHECK(code(KeyPress{.key = Key::Down}) == scan(80));
    SCI_CHECK(code(KeyPress{.key = Key::PageDown}) == scan(81));
    SCI_CHECK(code(KeyPress{.key = Key::Insert}) == scan(82));
    SCI_CHECK(code(KeyPress{.key = Key::Delete}) == scan(83));
}

SCI_TEST(function_keys_f1_to_f10_and_with_shift) {
    SCI_CHECK(code(function(1, false)) == scan(59));  // kSciKeyF1
    SCI_CHECK(code(function(10, false)) == scan(68)); // kSciKeyF10
    SCI_CHECK(code(function(1, true)) == scan(84));   // kSciKeyShiftF1
    SCI_CHECK(code(function(10, true)) == scan(93));  // kSciKeyShiftF10
    SCI_CHECK(code(function(0, false)) == -1);
    SCI_CHECK(code(function(11, false)) == -1); // Only F1 to F10 are mapped.
    const auto shifted = sci_key_event(function(2, true));
    SCI_CHECK(shifted && shifted->modifiers == sci::vm::modifier_shift);
}

SCI_TEST(ctrl_and_a_letter_is_the_control_character) {
    const auto ctrl_a = sci_key_event(KeyPress{.key = Key::Character, .character = 'a', .ctrl = true});
    SCI_CHECK(ctrl_a && ctrl_a->character == 1 && ctrl_a->modifiers == sci::vm::modifier_ctrl);
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = 'c', .ctrl = true}) == 3);
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = 'z', .ctrl = true}) == 26);
    // Only lower case letters, and only Ctrl alone (KBD_NON_STICKY == KBD_CTRL).
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = '1', .ctrl = true}) == '1');
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = 'A', .shift = true, .ctrl = true}) == 'A');
}

SCI_TEST(alt_and_a_key_is_its_ibm_scancode) {
    const auto alt_a = sci_key_event(KeyPress{.key = Key::Character, .character = 'a', .alt = true});
    SCI_CHECK(alt_a && alt_a->character == scan(0x1E) && alt_a->modifiers == sci::vm::modifier_alt);
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = 'q', .alt = true}) == scan(0x10));
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = 'P', .alt = true}) == scan(0x19));
    // scancodeAltifyRows counts on along the row: 0x29 for the backslash.
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = '\\', .alt = true}) == scan(0x29));
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = 'z', .alt = true}) == scan(0x2C));
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = '/', .alt = true}) == scan(0x35));
    // Keys off the three letter rows keep their character.
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = '1', .alt = true}) == '1');
    // A control character with Alt is first its letter (SDL1 with Ctrl+Alt),
    // and Alt wins over Ctrl.
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = 1, .ctrl = true, .alt = true}) == scan(0x1E));
    SCI_CHECK(code(KeyPress{.key = Key::Character, .character = 'a', .ctrl = true, .alt = true}) == scan(0x1E));
    // Enter (13) becomes 'm'; a scancode key has no low byte left.
    SCI_CHECK(code(KeyPress{.key = Key::Enter, .alt = true}) == scan(0x32));
    SCI_CHECK(code(KeyPress{.key = Key::Function, .function = 1, .alt = true}) == -1);
    const auto both = sci_key_event(KeyPress{.key = Key::Character, .character = 'a', .shift = true, .alt = true});
    SCI_CHECK(both && both->modifiers == (sci::vm::modifier_alt | sci::vm::modifier_shift));
}

SCI_TEST(keypad_keys_follow_num_lock) {
    // keyMappings: the navigation key without Num Lock, the digit with it.
    SCI_CHECK(code(keypad(Key::Insert, '0', false)) == scan(82));
    SCI_CHECK(code(keypad(Key::Insert, '0', true)) == '0');
    SCI_CHECK(code(keypad(Key::End, '1', false)) == scan(79));
    SCI_CHECK(code(keypad(Key::Center, '5', false)) == scan(76));
    SCI_CHECK(code(keypad(Key::Center, '5', true)) == '5');
    SCI_CHECK(code(keypad(Key::Up, '8', false)) == scan(72));
    SCI_CHECK(code(keypad(Key::Up, '8', true)) == '8');
    SCI_CHECK(code(keypad(Key::PageUp, '9', true)) == '9');
    SCI_CHECK(code(keypad(Key::Delete, '.', false)) == scan(83));
    SCI_CHECK(code(keypad(Key::Delete, '.', true)) == '.');
    // Enter and the operators are the same either way.
    SCI_CHECK(code(keypad(Key::Enter, 0, false)) == 13);
    SCI_CHECK(code(keypad(Key::Enter, 0, true)) == 13);
    SCI_CHECK(code(keypad(Key::Character, '+', false)) == '+');
    SCI_CHECK(code(keypad(Key::Character, '/', true)) == '/');
    // Num Lock does not touch the keys off the keypad.
    SCI_CHECK(code(KeyPress{.key = Key::Up, .num_lock = true}) == scan(72));
}

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

struct Pixel {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;

    bool operator==(const Pixel&) const = default;
};

Pixel pixel(const CursorImage& image, int x, int y) {
    const auto i = static_cast<std::size_t>((y * 16) + x) * 4;
    return Pixel{
        .r = image.pixels[i],
        .g = image.pixels[i + 1],
        .b = image.pixels[i + 2],
        .a = image.pixels[i + 3],
    };
}

constexpr Pixel black{.r = 0, .g = 0, .b = 0, .a = 0xFF};
constexpr Pixel white{.r = 0xFF, .g = 0xFF, .b = 0xFF, .a = 0xFF};

bool transparent(const CursorImage& image, int x, int y) {
    return pixel(image, x, y).a == 0;
}

} // namespace

SCI_TEST(cursor_masks_give_black_white_and_transparent) {
    const auto data = cursor_resource(0);
    const auto image = decode_cursor(data);
    SCI_CHECK(image.has_value());
    if (!image) {
        return;
    }
    SCI_CHECK(pixel(*image, 0, 0) == white); // Colour 3 is white in SCI0.
    SCI_CHECK(pixel(*image, 1, 0) == white); // Colour 1.
    SCI_CHECK(pixel(*image, 2, 0) == black); // Colour 0.
    SCI_CHECK(transparent(*image, 3, 0));    // Colour 2.
    SCI_CHECK(pixel(*image, 15, 0) == black);
    for (int x = 0; x < 16; ++x) {
        SCI_CHECK(transparent(*image, x, 1));
    }
    SCI_CHECK(transparent(*image, 0, 2) && transparent(*image, 14, 2));
    SCI_CHECK(pixel(*image, 15, 2) == white);
    SCI_CHECK(pixel(*image, 7, 15) == black);
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
