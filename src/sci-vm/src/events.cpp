// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/event.cpp (EventManager::getSciEvent, flushEvents)
// and translated into this project's style, types and error handling. The
// replay reader mirrors the reference patch's
// (tools/scummvm-patches/0003-deterministic-trace.patch).
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#include "events.hpp"

#include <charconv>
#include <format>
#include <optional>
#include <string>

namespace sci::vm {

void EventQueue::take_due(std::uint32_t tick) {
    while (!pending_.empty() && pending_.front().tick <= tick) {
        InputEvent input = pending_.front();
        pending_.pop_front();
        // Mouse events carry their position; key events the current one.
        if (input.event.type == event_key_down || input.event.type == event_key_up) {
            input.event.mouse = mouse_;
        } else {
            mouse_ = input.event.mouse;
        }
        if (!input.move_only && input.event.type != event_none) {
            queue_.push_back(input.event);
        }
    }
}

SciEvent EventQueue::get(std::uint16_t mask, std::uint32_t tick) {
    take_due(tick);
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if ((it->type & mask) != 0) {
            const SciEvent event = *it;
            if ((mask & event_peek) == 0) {
                queue_.erase(it);
            }
            return event;
        }
    }
    return SciEvent{.type = event_none, .modifiers = modifier_none, .character = 0, .mouse = mouse_};
}

void EventQueue::flush(std::uint32_t tick) {
    take_due(tick);
    queue_.clear();
}

namespace {

std::optional<long> parse_number(std::string_view& text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    long value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end == text.data()) {
        return std::nullopt;
    }
    text.remove_prefix(static_cast<std::size_t>(end - text.data()));
    return value;
}

std::string_view next_word(std::string_view& text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    const std::size_t end = text.find(' ');
    const std::string_view word = text.substr(0, end);
    text.remove_prefix(end == std::string_view::npos ? text.size() : end);
    return word;
}

SciEvent make_event(std::uint16_t type, std::uint16_t character, std::uint16_t modifiers, Point mouse) {
    return SciEvent{.type = type, .modifiers = modifiers, .character = character, .mouse = mouse};
}

} // namespace

core::Result<std::vector<InputEvent>> parse_replay(std::string_view text) {
    std::vector<InputEvent> events;
    bool header = false;
    Point position{.x = 160, .y = 100};
    std::size_t line_number = 0;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (!header) {
            if (line != "sierra-hd replay 1") {
                return core::make_error(core::ErrorCode::InvalidData, "not a version 1 replay file");
            }
            header = true;
            continue;
        }
        const auto bad = [&] {
            return core::make_error(core::ErrorCode::InvalidData, std::format("bad replay line {}", line_number));
        };
        const auto tick = parse_number(line);
        if (!tick || *tick < 0) {
            return bad();
        }
        const std::string_view kind = next_word(line);
        InputEvent input{.tick = static_cast<std::uint32_t>(*tick), .event = SciEvent{}, .move_only = false};
        if (kind == "text") {
            if (!line.empty() && line.front() == ' ') {
                line.remove_prefix(1);
            }
            for (const char c : line) {
                input.event = make_event(event_key_down, static_cast<std::uint8_t>(c), modifier_none, position);
                events.push_back(input);
            }
            input.event = make_event(event_key_down, key_enter, modifier_none, position);
            events.push_back(input);
            continue;
        }
        const auto a = parse_number(line);
        const auto b = parse_number(line);
        const auto c = parse_number(line);
        if (kind == "key" && a) {
            input.event = make_event(event_key_down, static_cast<std::uint16_t>(*a),
                                     static_cast<std::uint16_t>(b.value_or(0)), position);
        } else if ((kind == "down" || kind == "up") && a && b) {
            position = Point{.x = static_cast<std::int16_t>(*a), .y = static_cast<std::int16_t>(*b)};
            input.event = make_event(kind == "down" ? event_mouse_press : event_mouse_release, 0,
                                     static_cast<std::uint16_t>(c.value_or(0)), position);
        } else if (kind == "move" && a && b) {
            position = Point{.x = static_cast<std::int16_t>(*a), .y = static_cast<std::int16_t>(*b)};
            input.event = make_event(event_none, 0, 0, position);
            input.move_only = true;
        } else {
            return bad();
        }
        events.push_back(input);
    }
    if (!header) {
        return core::make_error(core::ErrorCode::InvalidData, "not a version 1 replay file");
    }
    return events;
}

} // namespace sci::vm
