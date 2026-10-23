// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window layer's input and cursor, made neutral of SDL: key presses as
// SCI key events (ScummVM's EventManager::getScummVMEvent, keyMappings and
// altify in engines/sci/event.cpp), the cursor scaled N x N (spec section
// 6.9) and how the system shows it, and the mouse from window coordinates
// to 1x and back, with letterbox and display scaling.

#include "game_screen.hpp"
#include "sci/view/cursor.hpp"
#include "sci/vm/events.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

using sci::app::cursor_mode;
using sci::app::CursorImage;
using sci::app::CursorMode;
using sci::app::FrameGeometry;
using sci::app::KeyPress;
using sci::app::scale_cursor;
using sci::app::sci_key_event;
using sci::app::window_from_1x;
using sci::app::window_to_1x;
using sci::app::WindowPoint;
using sci::view::CursorPixel;
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

// A cursor with a different pixel at each place: transparent, black and
// white in turn, the hotspot at (8, 8) as SCI0's centred flag sets it.
sci::view::Cursor sample_cursor() {
    sci::view::Cursor cursor;
    for (int y = 0; y < sci::view::Cursor::size; ++y) {
        for (int x = 0; x < sci::view::Cursor::size; ++x) {
            cursor.pixels[(static_cast<std::size_t>(y) * 16) + static_cast<std::size_t>(x)] =
                static_cast<CursorPixel>(((x * 3) + y) % 3);
        }
    }
    cursor.hotspot_x = 8;
    cursor.hotspot_y = 8;
    return cursor;
}

struct Rgba {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;

    bool operator==(const Rgba&) const = default;
};

Rgba pixel(const CursorImage& image, int x, int y) {
    const auto i =
        ((static_cast<std::size_t>(y) * static_cast<std::size_t>(image.size)) + static_cast<std::size_t>(x)) * 4;
    return Rgba{.r = image.pixels[i], .g = image.pixels[i + 1], .b = image.pixels[i + 2], .a = image.pixels[i + 3]};
}

Rgba expected(CursorPixel source) {
    switch (source) {
    case CursorPixel::Black:
        return Rgba{.r = 0, .g = 0, .b = 0, .a = 0xFF};
    case CursorPixel::White:
        return Rgba{.r = 0xFF, .g = 0xFF, .b = 0xFF, .a = 0xFF};
    case CursorPixel::Transparent:
        break;
    }
    return Rgba{.r = 0, .g = 0, .b = 0, .a = 0};
}

} // namespace

SCI_TEST(the_cursor_at_n_is_the_resource_scaled_n_by_n) {
    const sci::view::Cursor cursor = sample_cursor();
    for (int scale = 1; scale <= 16; ++scale) {
        const CursorImage image = scale_cursor(cursor, scale);
        SCI_CHECK(image.size == 16 * scale);
        SCI_CHECK(image.pixels.size() ==
                  static_cast<std::size_t>(image.size) * static_cast<std::size_t>(image.size) * 4);
        SCI_CHECK(image.hotspot_x == 8 * scale && image.hotspot_y == 8 * scale);
        bool all = true;
        for (int y = 0; y < image.size; ++y) {
            for (int x = 0; x < image.size; ++x) {
                all = all && pixel(image, x, y) == expected(cursor.at(x / scale, y / scale));
            }
        }
        SCI_CHECK(all);
    }
    // A top-left hotspot stays at 0.
    sci::view::Cursor top_left = cursor;
    top_left.hotspot_x = top_left.hotspot_y = 0;
    const CursorImage image = scale_cursor(top_left, 7);
    SCI_CHECK(image.hotspot_x == 0 && image.hotspot_y == 0);
}

SCI_TEST(the_system_shows_the_cursor_only_at_its_own_pixel_size) {
    SCI_CHECK(cursor_mode(1.0, 16) == CursorMode::System);
    SCI_CHECK(cursor_mode(1.0, 16 * 16) == CursorMode::System);
    // Windows cursor files hold at most 256 pixels per side.
    SCI_CHECK(cursor_mode(1.0, 16 * 17) == CursorMode::Drawn);
    // Where SDL takes cursor pixels as points, they would be scaled again.
    SCI_CHECK(cursor_mode(2.0, 16 * 5) == CursorMode::Drawn);
    SCI_CHECK(cursor_mode(1.25, 16 * 5) == CursorMode::Drawn);
    SCI_CHECK(cursor_mode(1.5, 16) == CursorMode::Drawn);
}

SCI_TEST(the_mouse_maps_to_1x_minus_the_letterbox_divided_by_n) {
    // N = 5 in a 1920 x 1080 output: the frame of 1600 x 1000 starts at
    // (160, 40).
    const FrameGeometry geometry{.scale = 5, .offset_x = 160, .offset_y = 40, .pixel_density = 1.0};
    SCI_CHECK(window_to_1x(160, 40, geometry) == (sci::vm::Point{.x = 0, .y = 0}));
    SCI_CHECK(window_to_1x(164.9, 44.9, geometry) == (sci::vm::Point{.x = 0, .y = 0}));
    SCI_CHECK(window_to_1x(165, 45, geometry) == (sci::vm::Point{.x = 1, .y = 1}));
    SCI_CHECK(window_to_1x(160 + 1599, 40 + 999, geometry) == (sci::vm::Point{.x = 319, .y = 199}));
    SCI_CHECK(window_to_1x(160 + (5 * 123) + 2, 40 + (5 * 77) + 4, geometry) == (sci::vm::Point{.x = 123, .y = 77}));
    // In the letterbox: clamped to the screen's edge.
    SCI_CHECK(window_to_1x(0, 0, geometry) == (sci::vm::Point{.x = 0, .y = 0}));
    SCI_CHECK(window_to_1x(159.5, 39.5, geometry) == (sci::vm::Point{.x = 0, .y = 0}));
    SCI_CHECK(window_to_1x(1919, 1079, geometry) == (sci::vm::Point{.x = 319, .y = 199}));
    SCI_CHECK(window_to_1x(-30, 2000, geometry) == (sci::vm::Point{.x = 0, .y = 199}));
}

SCI_TEST(display_scaling_maps_window_coordinates_to_physical_pixels) {
    // 150 %: a window coordinate is 1.5 physical pixels. N = 4 in 1500 x
    // 900 physical pixels: the frame of 1280 x 800 starts at (110, 50).
    const FrameGeometry scaled{.scale = 4, .offset_x = 110, .offset_y = 50, .pixel_density = 1.5};
    // The physical pixel (110, 50) is at window (73.33, 33.33).
    SCI_CHECK(window_to_1x(73.34, 33.34, scaled) == (sci::vm::Point{.x = 0, .y = 0}));
    SCI_CHECK(window_to_1x(73.3, 33.3, scaled) == (sci::vm::Point{.x = 0, .y = 0})); // Letterbox, clamped.
    SCI_CHECK(window_to_1x((110 + 4.0) / 1.5, (50 + 8.0) / 1.5, scaled) == (sci::vm::Point{.x = 1, .y = 2}));
    SCI_CHECK(window_to_1x((110 + 3.9) / 1.5, (50 + 7.9) / 1.5, scaled) == (sci::vm::Point{.x = 0, .y = 1}));
    // 125 % and Retina's 2.
    const FrameGeometry quarter{.scale = 3, .offset_x = 0, .offset_y = 0, .pixel_density = 1.25};
    SCI_CHECK(window_to_1x(100, 100, quarter) == (sci::vm::Point{.x = 41, .y = 41})); // Pixel 125.
    const FrameGeometry retina{.scale = 6, .offset_x = 20, .offset_y = 0, .pixel_density = 2.0};
    SCI_CHECK(window_to_1x(10, 0, retina) == (sci::vm::Point{.x = 0, .y = 0}));
    SCI_CHECK(window_to_1x(13, 3, retina) == (sci::vm::Point{.x = 1, .y = 1})); // Pixel (26, 6).
}

SCI_TEST(the_game_mouse_goes_to_the_centre_of_its_block_and_back) {
    for (const double density : {1.0, 1.25, 1.5, 2.0}) {
        for (const int scale : {1, 2, 3, 5, 10, 16}) {
            const FrameGeometry geometry{.scale = scale, .offset_x = 37, .offset_y = 11, .pixel_density = density};
            bool all = true;
            for (int y = 0; y < 200; y += 7) {
                for (int x = 0; x < 320; x += 3) {
                    const sci::vm::Point point{.x = static_cast<std::int16_t>(x), .y = static_cast<std::int16_t>(y)};
                    const WindowPoint window = window_from_1x(point, geometry);
                    all = all && window_to_1x(window.x, window.y, geometry) == point;
                }
            }
            SCI_CHECK(all);
        }
    }
    const FrameGeometry geometry{.scale = 4, .offset_x = 10, .offset_y = 20, .pixel_density = 1.0};
    const WindowPoint centre = window_from_1x(sci::vm::Point{.x = 2, .y = 3}, geometry);
    SCI_CHECK(centre.x == 20.0 && centre.y == 34.0);
}
