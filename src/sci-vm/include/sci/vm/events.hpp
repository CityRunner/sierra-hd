// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input to the interpreter: SCI events (engines/sci/event.h in ScummVM) and
// the replay file format (docs/formats/replay-and-trace.md).

#pragma once

#include "sci/core/error.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace sci::vm {

struct Point {
    std::int16_t x = 0;
    std::int16_t y = 0;

    friend constexpr bool operator==(Point, Point) noexcept = default;
};

// Event types and key codes of SCI16.
enum EventType : std::uint16_t {
    event_none = 0,
    event_mouse_press = 1,
    event_mouse_release = 1 << 1,
    event_key_down = 1 << 2,
    event_key_up = 1 << 3,
    event_direction = 1 << 6,
    event_said = 1 << 7,
    event_quit = 1 << 11,
    event_peek = 1 << 15,
};

enum KeyCode : std::uint16_t {
    key_enter = 13,
    key_escape = 27,
    key_up = 72 << 8,
    key_down = 80 << 8,
    key_left = 75 << 8,
    key_right = 77 << 8,
};

enum KeyModifier : std::uint16_t {
    modifier_none = 0,
    modifier_shift = 3,
    modifier_ctrl = 1 << 2,
    modifier_alt = 1 << 3,
};

struct SciEvent {
    std::uint16_t type = event_none;
    std::uint16_t modifiers = modifier_none;
    std::uint16_t character = 0;
    Point mouse;
};

// An input event due at a game tick. A `move` event only moves the mouse.
struct InputEvent {
    std::uint32_t tick = 0;
    SciEvent event;
    bool move_only = false;
};

// Reads a replay file (`sierra-hd replay 1`).
[[nodiscard]] core::Result<std::vector<InputEvent>> parse_replay(std::string_view text);

} // namespace sci::vm
