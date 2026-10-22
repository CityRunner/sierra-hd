// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window layer's part of the game screen: the game's cursor and the
// translation of keys (the frame itself is composed by
// sci/platform/game_frame.hpp).
#pragma once

#include "sci/vm/events.hpp"
#include "sci/vm/game.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sci::app {

// An SCI0 cursor resource as RGBA pixels (16 x 16) and its hotspot.
struct CursorImage {
    std::array<std::uint8_t, std::size_t{16} * 16 * 4> pixels{};
    int hotspot_x = 0;
    int hotspot_y = 0;
};

[[nodiscard]] std::optional<CursorImage> decode_cursor(std::span<const std::uint8_t> resource);

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
