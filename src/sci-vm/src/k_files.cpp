// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kfile.cpp (kFileIO open, close, readString and
//   writeString as SCI0's FOpen, FClose, FGets and FPuts; kSaveGame,
//   kRestoreGame, kCheckSaveGame, kGetSaveFiles with no saved games: the save
//   format is milestone 8)
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

#include "files.hpp"
#include "graphics.hpp"
#include "kernel_functions.hpp"
#include "machine.hpp"
#include "subsystems.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace sci::vm {

namespace {

constexpr int save_id_first = 100; // SAVEGAMEID_OFFICIALRANGE_START
constexpr int save_id_last = 199;  // SAVEGAMEID_OFFICIALRANGE_END
constexpr Reg signal_reg = make_reg(0, 0xFFFF);

Files& files(Machine& machine) {
    return machine.subsystems().files();
}

} // namespace

Reg k_file_io_open(Machine& machine, std::span<Reg> args) {
    std::string name = machine.segments().get_string(args[0]);
    const int mode = args[1].to_uint16();
    // SQ4 floppy and QFG4 put "/\" before the names.
    if (name.starts_with("/\\")) {
        name.erase(0, 2);
    }
    if (name.empty()) {
        return signal_reg;
    }
    // The English part of the name, in lower case.
    name = machine.subsystems().graphics().language_text(name, 1);
    std::ranges::transform(name, name.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    std::string error;
    const std::uint16_t handle = files(machine).open(name, mode, error);
    if (!error.empty()) {
        machine.faults().raise(error);
    }
    return number(handle);
}

Reg k_file_io_close(Machine& machine, std::span<Reg> args) {
    if (args[0] == signal_reg) {
        return machine.acc();
    }
    (void)files(machine).close(args[0].to_uint16());
    return machine.acc(); // SCI0 returns no value.
}

Reg k_file_io_read_string(Machine& machine, std::span<Reg> args) {
    std::size_t max_size = args[1].to_uint16();
    std::string error;
    const auto line = files(machine).read_line(args[2].to_uint16(), static_cast<int>(max_size), error);
    if (!error.empty()) {
        machine.faults().raise(error);
        return null_reg;
    }
    const std::string read = line.value_or(std::string{});
    const SegmentRef dest = machine.segments().dereference(args[0]);
    if (!dest.valid()) {
        machine.faults().raise(
            std::format("kFileIO(readString): invalid destination {:04x}:{:04x}", args[0].segment, args[0].offset));
        return null_reg;
    }
    if (std::cmp_greater(read.size(), dest.max_size)) {
        machine.faults().raise(std::format("kFileIO(readString) attempting to read {} bytes into buffer of size {}",
                                           read.size(), dest.max_size));
        return null_reg;
    }
    max_size = std::min(max_size, static_cast<std::size_t>(dest.max_size));
    // The buffer of max_size bytes, zero after the line.
    std::vector<std::uint8_t> buffer(max_size, 0);
    std::copy_n(read.begin(), std::min(read.size(), max_size), buffer.begin());
    machine.segments().memcpy(args[0], buffer);
    return read.empty() ? null_reg : args[0];
}

Reg k_file_io_write_string(Machine& machine, std::span<Reg> args) {
    const std::string text = machine.segments().get_string(args[1]);
    const auto written = files(machine).write(args[0].to_uint16(), text);
    return written ? number(static_cast<int>(*written)) : null_reg;
}

// Saved games: none are listed or written before milestone 8.

Reg k_save_game(Machine& machine, std::span<Reg> args) {
    if (machine.execution_stack_base() != 0) {
        warn("kSaveGame - won't save from within kernel function");
        return null_reg;
    }
    if (args[0].is_null()) {
        // ScummVM's own save dialog; the game's dialogs are used here.
        machine.faults().raise("kSaveGame: the ScummVM save dialog is not used");
        return null_reg;
    }
    if (args[2].is_null()) {
        machine.faults().raise("kSaveGame: called with description being NULL");
        return null_reg;
    }
    const int virtual_id = args[1].to_sint16();
    if (virtual_id > save_id_last) {
        machine.faults().raise("kSaveGame: invalid savegameId used");
        return null_reg;
    }
    if (virtual_id >= save_id_first) {
        return null_reg; // No such saved game to overwrite.
    }
    warn("kSaveGame: saving comes with milestone 8; the game is not saved");
    return null_reg;
}

Reg k_restore_game(Machine& machine, std::span<Reg> args) {
    const int id = args[1].to_sint16();
    if (args[0].is_null()) {
        machine.faults().raise("kRestoreGame: the ScummVM restore dialog is not used");
        return machine.acc();
    }
    if (id < save_id_first || id > save_id_last) {
        warn(std::format("Savegame ID {} is not allowed", id));
        return true_reg;
    }
    warn(std::format("Savegame ID {} not found", id - save_id_first));
    return true_reg; // Failure.
}

Reg k_check_save_game(Machine& machine, std::span<Reg> args) {
    const std::uint16_t virtual_id = args[1].to_uint16();
    if (virtual_id == 0) {
        return null_reg;
    }
    if (virtual_id < save_id_first || virtual_id > save_id_last) {
        machine.faults().raise(std::format("kCheckSaveGame: called with invalid savegame ID ({})", virtual_id));
        return null_reg;
    }
    return null_reg; // Not found.
}

Reg k_get_save_files(Machine& machine, std::span<Reg> args) {
    // No saved games: an empty list of names (its terminating zero).
    const std::array<std::uint8_t, 1> names{0};
    machine.segments().memcpy(args[1], names);
    return number(0);
}

} // namespace sci::vm
