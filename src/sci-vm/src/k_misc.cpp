// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kmisc.cpp, kfile.cpp, kevent.cpp, kparse.cpp (SCI0
//   functions; kGetTime with the fixed date of the deterministic mode)
// and translated into this project's style, types and error handling.
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
#include "graphics.hpp"
#include "kernel_functions.hpp"
#include "machine.hpp"
#include "parser.hpp"
#include "path_match.hpp"
#include "sound.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <array>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

namespace sci::vm {

namespace {

constexpr std::uint32_t speed_throttle_delay = 30; // kSpeedThrottleDefaultDelay, in ms.

// 1990-01-01 12:00:00, the date and time of the deterministic mode.

struct KeyDirection {
    std::uint16_t key;
    std::uint16_t direction;
};

constexpr std::array<KeyDirection, 9> key_directions{
    {
        {.key = 71U << 8U, .direction = 8},
        {.key = 72U << 8U, .direction = 1},
        {.key = 73U << 8U, .direction = 2},
        {.key = 75U << 8U, .direction = 7},
        {.key = 76U << 8U, .direction = 0},
        {.key = 77U << 8U, .direction = 3},
        {.key = 79U << 8U, .direction = 6},
        {.key = 80U << 8U, .direction = 5},
        {.key = 81U << 8U, .direction = 4},
    },
};

} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to Common::matchString.
bool match_path(std::string_view str, std::string_view pattern) {
    const auto at = [](std::string_view text, std::size_t i) { return i < text.size() ? text[i] : '\0'; };
    std::size_t s = 0;
    std::size_t p = 0;
    std::optional<std::size_t> back_pattern;
    std::size_t back_string = 0;
    bool escaped = false;
    for (;;) {
        const char c = at(str, s);
        if (c == '\0' || c == '/') {
            back_pattern.reset();
            if (at(pattern, p) == '?') {
                return false;
            }
        }
        const char current = at(pattern, p);
        bool literal = false;
        switch (current) {
        case '*':
            if (c != '\0') {
                back_pattern = ++p;
                back_string = s;
            } else {
                ++p;
                back_pattern.reset();
            }
            break; // With exclusions, a trailing * is no early match.
        case '\\':
            if (!escaped) {
                ++p;
                break;
            }
            literal = true;
            break;
        case '#':
            if (!escaped) {
                if (c < '0' || c > '9') {
                    return false;
                }
                ++p;
                ++s;
                break;
            }
            literal = true;
            break;
        case '?':
            if (c == '\0') {
                return at(pattern, p) == '\0';
            }
            ++p;
            ++s;
            break;
        default:
            literal = true;
            break;
        }
        if (literal) {
            if (current != c) {
                if (!back_pattern) {
                    return false;
                }
                p = *back_pattern;
                s = ++back_string;
                if (at(str, s) == '\0') {
                    return at(pattern, p) == '\0';
                }
            } else {
                if (c == '\0') {
                    return at(pattern, p) == '\0';
                }
                ++p;
                ++s;
            }
        }
        escaped = !escaped && current == '\\';
    }
}

Reg k_restart_game(Machine& machine, std::span<Reg> /*args*/) {
    machine.shrink_stack_to_base();
    machine.abort(Abort::RestartGame);
    return null_reg;
}

Reg k_game_is_restarting(Machine& machine, std::span<Reg> args) {
    const auto previous = static_cast<std::uint16_t>(machine.game_is_restarting() ? 1 : 0);
    if (args.empty() || args[0].to_uint16() != 0) {
        return number(previous);
    }
    machine.clear_game_is_restarting();
    // The speed throttle of ScummVM; no game-specific delay for this game.
    machine.subsystems().clock().speed_throttler(speed_throttle_delay);
    machine.subsystems().reset_event_counter();
    return number(previous);
}

Reg k_have_mouse(Machine& /*machine*/, std::span<Reg> /*args*/) {
    return make_reg(0, 0xFFFF); // SIGNAL_REG
}

Reg k_memory_info(Machine& machine, std::span<Reg> args) {
    // Not 0xFFFF, or some memory calculations overflow.
    constexpr std::uint16_t size = 0x7FEA;
    switch (args[0].offset) {
    case 0: // The largest heap block: the free heap less 2 avoids "memory fragmented".
        return number(size - 2);
    case 1:
    case 2:
    case 3:
    case 4:
        return number(size);
    default:
        machine.faults().raise(std::format("Unknown MemoryInfo operation: {:04x}", args[0].offset));
        return null_reg;
    }
}

Reg k_flush_resources(Machine& machine, std::span<Reg> /*args*/) {
    machine.collect_garbage();
    return machine.acc();
}

// NOLINTNEXTLINE(misc-const-correctness): the kernel function signature.
Reg k_set_debug(Machine& machine, std::span<Reg> /*args*/) {
    warn("Debug mode activated");
    return machine.acc();
}

Reg k_get_time(Machine& machine, std::span<Reg> args) {
    const int mode = !args.empty() ? args[0].to_uint16() : 0;
    unsigned value = 0;
    const LocalTime now = mode != 0 ? machine.subsystems().clock().local_time() : LocalTime{};
    const auto hour24 = static_cast<unsigned>(now.hour);
    const auto minute = static_cast<unsigned>(now.minute);
    const auto second = static_cast<unsigned>(now.second);
    switch (mode) {
    case 0:
        machine.subsystems().clock().read_elapsed(machine.kernel_calls());
        value = machine.subsystems().clock().ticks();
        break;
    case 1: {
        unsigned hour = hour24 % 12;
        if (hour == 0) {
            hour = 12;
        }
        value = (hour << 12U) | (minute << 6U) | second;
        break;
    }
    case 2:
        value = (hour24 << 11U) | (minute << 5U) | (second >> 1U);
        break;
    case 3: {
        // SCI0 late on DOS: years since 1920.
        constexpr unsigned base_year = 20;
        value = static_cast<unsigned>(now.day) | ((static_cast<unsigned>(now.month) + 1U) << 5U) |
                (((static_cast<unsigned>(now.year) - base_year) & 0x7FU) << 9U);
        break;
    }
    default:
        machine.faults().raise(std::format("Attempt to use unknown GetTime mode {}", mode));
        return null_reg;
    }
    return number(static_cast<std::uint16_t>(value));
}

// NOLINTNEXTLINE(misc-const-correctness): the kernel function signature.
Reg k_empty(Machine& machine, std::span<Reg> /*args*/) {
    return machine.acc();
}

Reg k_dummy(Machine& machine, std::span<Reg> /*args*/) {
    machine.faults().raise("Kernel function was called, which was considered to be unused - see log for details");
    return null_reg;
}

// Files: the scripts see no file system.

Reg k_get_save_dir(Machine& machine, std::span<Reg> /*args*/) {
    return machine.segments().save_dir_pointer();
}

Reg k_get_cwd(Machine& machine, std::span<Reg> args) {
    machine.segments().strcpy(args[0], "/");
    return args[0];
}

Reg k_check_free_space(Machine& machine, std::span<Reg> args) {
    const int subop = args.size() > 1 ? args[1].to_sint16() : 2;
    switch (subop) {
    case 0: // The save game size.
        return number(0);
    case 1: // Free disk space in KiB.
        return number(0x7FFF);
    case 2: // Enough space to save.
        return number(1);
    default:
        machine.faults().raise(std::format("kCheckFreeSpace: called with unknown sub-op {}", subop));
        return null_reg;
    }
}

Reg k_valid_path(Machine& /*machine*/, std::span<Reg> /*args*/) {
    return number(1);
}

Reg k_device_info(Machine& machine, std::span<Reg> args) {
    const SegManager& segments = machine.segments();
    switch (args[0].to_uint16()) {
    case 0: // The device of a path.
        segments.strcpy(args[2], "/");
        break;
    case 1: // The current device.
        segments.strcpy(args[1], "/");
        break;
    case 2: // Paths equal.
        return number(match_path(segments.get_string(args[2]), segments.get_string(args[1])) ? 1 : 0);
    case 3: // Is floppy: never.
    case 4: // The configuration path: none.
        return null_reg;
    case 5: // The save catalogue's name, for deleting saves.
        segments.strcpy(args[1], "__throwaway");
        break;
    case 6: { // A save file's name, for deleting it.
        segments.strcpy(args[1], "__throwaway");
        // SAVEGAMEID_OFFICIALRANGE_START and _END. Until the save format of
        // milestone 8 there are no saves, so none is deleted.
        constexpr int first_save_id = 100;
        constexpr int last_save_id = 199;
        const int virtual_id = args[3].to_uint16();
        if (virtual_id < first_save_id || virtual_id > last_save_id) {
            machine.faults().raise("kDeviceInfo(deleteSave): invalid savegame ID specified");
            return null_reg;
        }
        break;
    }
    default:
        machine.faults().raise(std::format("Unknown DeviceInfo() sub-command: {}", args[0].to_uint16()));
        return null_reg;
    }
    return machine.acc();
}

// Events.

Reg k_get_event(Machine& machine, std::span<Reg> args) {
    Subsystems& subsystems = machine.subsystems();
    const SelectorCache& s = machine.selector();
    const std::uint16_t mask = args[0].to_uint16();
    const Reg object = args[1];
    const SciEvent event = subsystems.events().get(mask, subsystems.clock().ticks());
    const Point mouse = event.mouse;
    subsystems.parser().invalidate_event();
    machine.write_selector_value(object, s.x, mouse.x);
    machine.write_selector_value(object, s.y, mouse.y);
    // SCI0 on DOS: all modifier bits, and 0x02 in the high byte (the BIOS
    // shift status call left it there).
    const auto modifiers = static_cast<std::uint16_t>(event.modifiers | 0x0200U);
    switch (event.type) {
    case event_quit:
        machine.abort(Abort::QuitGame);
        break;
    case event_key_down:
    case event_key_up:
        machine.write_selector_value(object, s.type, event.type);
        machine.write_selector_value(object, s.message, event.character);
        machine.write_selector_value(object, s.modifiers, modifiers);
        machine.set_acc(true_reg);
        break;
    case event_mouse_release:
    case event_mouse_press:
        if ((mask & event.type) != 0) {
            machine.write_selector_value(object, s.type, event.type);
            machine.write_selector_value(object, s.message, 0);
            machine.write_selector_value(object, s.modifiers, modifiers);
            machine.set_acc(true_reg);
        }
        break;
    default:
        machine.write_selector_value(object, s.type, event_none);
        machine.write_selector_value(object, s.message, 0);
        machine.write_selector_value(object, s.modifiers, modifiers);
        machine.set_acc(null_reg);
        break;
    }
    // SCI0 sound does not poll for cues; kGetEvent does it.
    subsystems.sound().update_sci0_cues(machine);
    // An unthrottled inner loop: wait 10 ms.
    if (subsystems.count_event() > 2) {
        subsystems.clock().advance(10);
    }
    return machine.acc();
}

Reg k_map_key_to_dir(Machine& machine, std::span<Reg> args) {
    const Reg object = args[0];
    const SelectorCache& s = machine.selector();
    if (std::cmp_not_equal(machine.read_selector_value(object, s.type), event_key_down)) {
        return machine.acc();
    }
    const auto message = static_cast<std::uint16_t>(machine.read_selector_value(object, s.message));
    for (const KeyDirection& entry : key_directions) {
        if (entry.key == message) {
            machine.write_selector_value(object, s.type, event_direction);
            machine.write_selector_value(object, s.message, entry.direction);
            return true_reg;
        }
    }
    return null_reg;
}

Reg k_global_to_local(Machine& machine, std::span<Reg> args) {
    const Reg object = args[0];
    if (object.segment != 0) {
        const SelectorCache& s = machine.selector();
        const Port* port = machine.subsystems().graphics().ports().port();
        machine.write_selector_value(object, s.x, machine.read_selector_value(object, s.x) - port->left);
        machine.write_selector_value(object, s.y, machine.read_selector_value(object, s.y) - port->top);
    }
    return machine.acc();
}

Reg k_local_to_global(Machine& machine, std::span<Reg> args) {
    const Reg object = args[0];
    if (object.segment != 0) {
        const SelectorCache& s = machine.selector();
        const Port* port = machine.subsystems().graphics().ports().port();
        machine.write_selector_value(object, s.x, machine.read_selector_value(object, s.x) + port->left);
        machine.write_selector_value(object, s.y, machine.read_selector_value(object, s.y) + port->top);
    }
    return machine.acc();
}

Reg k_joystick(Machine& /*machine*/, std::span<Reg> /*args*/) {
    return null_reg;
}

// The parser's synonyms.

Reg k_set_synonyms(Machine& machine, std::span<Reg> args) {
    const SegManager& segments = machine.segments();
    const SelectorCache& s = machine.selector();
    Parser& parser = machine.subsystems().parser();
    parser.clear_synonyms();
    const List* list = segments.lookup_list(machine.read_selector(args[0], s.elements));
    if (list == nullptr) {
        machine.faults().raise("kSetSynonyms: no list of elements");
        return machine.acc();
    }
    const Node* node = segments.lookup_node(list->first);
    int count = 0;
    while (node != nullptr) {
        const int script = machine.read_selector_value(node->value, s.number);
        const SegmentId segment = segments.script_segment(script);
        if (segment > 0) {
            count = segments.script(segment)->synonyms_count();
        }
        if (count != 0) {
            const std::span<const std::uint8_t> synonyms = segments.script(segment)->synonyms();
            if (synonyms.empty()) {
                warn(std::format("Synonyms of script.{:03} were requested, but script is not available", script));
            } else if (count > 16384) {
                machine.faults().raise(std::format("Segtable corruption: script.{:03} has {} synonyms", script, count));
                return machine.acc();
            } else {
                for (std::size_t i = 0; std::cmp_less(i, count); ++i) {
                    const auto read16 = [&](std::size_t at) {
                        return static_cast<std::uint16_t>(static_cast<unsigned>(synonyms[at]) |
                                                          (static_cast<unsigned>(synonyms[at + 1]) << 8U));
                    };
                    parser.add_synonym(read16(i * 4), read16((i * 4) + 2));
                }
            }
        }
        node = segments.lookup_node(node->succ);
    }
    return machine.acc();
}

} // namespace sci::vm
