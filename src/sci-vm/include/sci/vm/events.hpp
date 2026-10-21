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
inline constexpr std::uint16_t event_none = 0;
inline constexpr std::uint16_t event_mouse_press = 1;
inline constexpr std::uint16_t event_mouse_release = 1U << 1U;
inline constexpr std::uint16_t event_key_down = 1U << 2U;
inline constexpr std::uint16_t event_key_up = 1U << 3U;
inline constexpr std::uint16_t event_direction = 1U << 6U;
inline constexpr std::uint16_t event_said = 1U << 7U;
inline constexpr std::uint16_t event_quit = 1U << 11U;
// Every event type (kSciEventAny, all but peek).
inline constexpr std::uint16_t event_any = 0x7FFF;
inline constexpr std::uint16_t event_peek = 1U << 15U;
inline constexpr std::uint16_t key_enter = 13;
inline constexpr std::uint16_t key_escape = 27;
inline constexpr std::uint16_t key_up = 72U << 8U;
inline constexpr std::uint16_t key_down = 80U << 8U;
inline constexpr std::uint16_t key_left = 75U << 8U;
inline constexpr std::uint16_t key_right = 77U << 8U;
inline constexpr std::uint16_t modifier_none = 0;
inline constexpr std::uint16_t modifier_shift = 3;
inline constexpr std::uint16_t modifier_ctrl = 1U << 2U;
inline constexpr std::uint16_t modifier_alt = 1U << 3U;

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
