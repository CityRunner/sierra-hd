// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window layer's part of the game screen: the game's cursor, the
// mouse in 1x and the translation of keys (the frame itself is composed by
// sci/platform/game_frame.hpp).
#pragma once

#include "sci/view/cursor.hpp"
#include "sci/vm/events.hpp"
#include "sci/vm/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sci::app {

// The game's cursor at N (spec section 6.9): the cursor resource's 16 x 16
// pixels scaled N x N as RGBA (black, white or transparent), and its
// hotspot scaled N times.
struct CursorImage {
    int size = 0; // Pixels per side: 16N.
    std::vector<std::uint8_t> pixels;
    int hotspot_x = 0;
    int hotspot_y = 0;
};

[[nodiscard]] CursorImage scale_cursor(const view::Cursor& cursor, int scale);

// How the program shows the cursor. The system's cursor moves without
// waiting for a frame, but systems size it in their own units: SDL takes
// its pixels as points where the window's pixel density is not 1 (macOS,
// scaled Wayland), and Windows cursor files hold at most 256 pixels per
// side. Elsewhere the program draws the cursor over the frame itself.
enum class CursorMode : std::uint8_t {
    System,
    Drawn,
};

[[nodiscard]] CursorMode cursor_mode(double pixel_density, int size);

// Where the frame is in the window: N and the letterbox, in physical
// pixels, and the window's pixel density (physical pixels per window
// coordinate; 1.25 or 1.5 with display scaling on some systems).
struct FrameGeometry {
    int scale = 1;
    int offset_x = 0;
    int offset_y = 0;
    double pixel_density = 1.0;
};

// A mouse position in window coordinates as the game takes it: the
// physical pixel under it, minus the letterbox, divided by N, clamped to
// the screen (spec section 6.9).
[[nodiscard]] vm::Point window_to_1x(double x, double y, const FrameGeometry& geometry);

// Where the game's mouse position is in window coordinates: the centre of
// its N x N block, so that window_to_1x gives it back.
struct WindowPoint {
    double x = 0;
    double y = 0;
};

[[nodiscard]] WindowPoint window_from_1x(vm::Point point, const FrameGeometry& geometry);

// A key press as the window layer sees it, made neutral of SDL.
struct KeyPress {
    enum class Key : std::uint8_t {
        Character, // `character` holds the ASCII character (with Shift applied).
        Enter,
        Escape,
        Backspace,
        Tab,
        Up,
        Down,
        Left,
        Right,
        Home,
        End,
        PageUp,
        PageDown,
        Insert,
        Delete,
        Center,   // Keypad 5.
        Function, // F1 to F10 in `function`.
        Other,
    };
    Key key = Key::Other;
    char character = 0;
    int function = 0;
    bool keypad = false; // Keypad digits, period, enter and operators.
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool num_lock = false;
};

// EventManager::getScummVMEvent for a key: the SCI event, or none.
[[nodiscard]] std::optional<vm::SciEvent> sci_key_event(const KeyPress& key);

} // namespace sci::app
