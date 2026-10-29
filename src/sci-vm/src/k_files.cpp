// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kfile.cpp (kFileIO open, close, readString and
//   writeString as SCI0's FOpen, FClose, FGets and FPuts; kSaveGame,
//   kRestoreGame, kCheckSaveGame, kGetSaveFiles),
//   engines/sci/engine/file.cpp (listSavegames, findSavegame),
//   engines/sci/engine/savegame.cpp (gamestate_restore after the state is
//   read)
// and translated into this project's style, types and error handling. The
// saves are in the project's format (docs/formats/save-format.md).
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
#include "gfx_screen.hpp"
#include "graphics.hpp"
#include "kernel_functions.hpp"
#include "machine.hpp"
#include "music.hpp"
#include "save_game.hpp"
#include "sound.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <tuple>

namespace sci::vm {

namespace {

constexpr Reg signal_reg = make_reg(0, 0xFFFF);
constexpr std::size_t max_save_name_length = 36; // kMaxSaveNameLength, with the terminator.
constexpr std::size_t max_listed_saves = 20;     // kMaxNumSaveGames

Files& files(Machine& machine) {
    return machine.subsystems().files();
}

// A saved game as the dialogs list it (SavegameDesc).
struct SaveDesc {
    int slot = 0;
    std::string name;
    LocalTime saved_at;
    // Of this format version and this release of the game, and undamaged
    // (read_save_header accepts it): it can be restored.
    bool compatible = false;
};

// listSavegames: the saves that have a description, newest first (by date
// and time; saves of the same second by slot). Files that are not saves
// are left out.
std::vector<SaveDesc> list_saves(Machine& machine) {
    std::vector<SaveDesc> saves;
    SaveStore* store = files(machine).saves();
    if (store == nullptr) {
        return saves;
    }
    const core::Sha256Digest game = game_identity(machine.resources().manager());
    for (const int slot : store->slots()) {
        if (slot < save_slot_first || slot > save_slot_last) {
            continue;
        }
        const auto file = store->read(slot);
        if (!file) {
            continue;
        }
        const auto header = read_save_summary(*file);
        if (!header || header->description.empty()) {
            continue;
        }
        std::string name = header->description;
        if (name.ends_with('\n')) {
            name.pop_back();
        }
        saves.push_back(SaveDesc{
            .slot = slot,
            .name = std::move(name),
            .saved_at = header->saved_at,
            .compatible = read_save_header(*file, &game).has_value(),
        });
    }
    const auto key = [](const SaveDesc& save) {
        const LocalTime& t = save.saved_at;
        return std::tuple(t.year, t.month, t.day, t.hour, t.minute, t.second);
    };
    std::ranges::stable_sort(saves, [&](const SaveDesc& a, const SaveDesc& b) {
        if (key(a) != key(b)) {
            return key(a) > key(b);
        }
        return a.slot < b.slot;
    });
    return saves;
}

const SaveDesc* find_save(const std::vector<SaveDesc>& saves, int slot) {
    const auto found = std::ranges::find(saves, slot, &SaveDesc::slot);
    return found != saves.end() ? &*found : nullptr;
}

// gamestate_restore after the state is read. First the host's time goes on
// from before the restore (the state brought the saved clock, which the
// layout file would show); then, in ScummVM's order: the screen is
// cleared (before reading there, but the maps are not in ScummVM's saves,
// so the result is the same), the picture window is set (it came back with
// the ports; EngineState::saveLoadWithSerializer sets it through
// kernelSetPicWindow, which the layout file records), the engine state is
// reset, the songs of the game before are cleared with the restored music
// settings (the device sees it: the play switch, volumes, the reverb, notes
// off), the restored songs get their parsers' volumes and new ordering times
// and those that were playing are played again; then runGame sets ScummVM's master volume and calls the
// game's replay method.
void after_restore(Machine& machine, const Subsystems::HostState& host) {
    Subsystems& subsystems = machine.subsystems();
    subsystems.after_restore(host);
    subsystems.graphics().screen().clear_for_restore();
    Ports& ports = subsystems.graphics().ports();
    if (const Window* pic = ports.pic_window()) {
        ports.kernel_set_pic_window(pic->rect, pic->top, pic->left, false);
    }
    machine.after_restore();
    if (subsystems.sound().has_music()) {
        subsystems.sound().music().after_restore(host.music);
    }
    subsystems.sound().reconstruct_play_list(machine);
    machine.segments().after_restore();
    if (subsystems.sound().has_music() && subsystems.sound().music().all_paused()) {
        subsystems.sound().music().reset_global_pause();
    }
    // SciEngine::syncSoundSettings when runGame takes the restore, before
    // the replay method: the master volume of ScummVM's setting (the one
    // before the restore) on the restored songs.
    if (subsystems.sound().has_music()) {
        subsystems.sound().music().set_master_volume(host.master_volume);
    }
    machine.abort(Abort::LoadGame);
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
    name = Graphics::language_text(name, 1);
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
    const std::string read = line ? line->text : std::string{};
    const std::size_t bytes_read = line ? line->bytes : 0;
    const SegmentRef dest = machine.segments().dereference(args[0]);
    if (!dest.valid()) {
        machine.faults().raise(
            std::format("kFileIO(readString): invalid destination {:04x}:{:04x}", args[0].segment, args[0].offset));
        return null_reg;
    }
    if (std::cmp_greater(bytes_read, dest.max_size)) {
        machine.faults().raise(std::format("kFileIO(readString) attempting to read {} bytes into buffer of size {}",
                                           bytes_read, dest.max_size));
        return null_reg;
    }
    max_size = std::min(max_size, static_cast<std::size_t>(dest.max_size));
    // The buffer of max_size bytes, zero after the line.
    std::vector<std::uint8_t> buffer(max_size, 0);
    std::copy_n(read.begin(), std::min(read.size(), max_size), buffer.begin());
    machine.segments().memcpy(args[0], buffer);
    return bytes_read == 0 ? null_reg : args[0];
}

Reg k_file_io_write_string(Machine& machine, std::span<Reg> args) {
    const std::string text = machine.segments().get_string(args[1]);
    const auto written = files(machine).write(args[0].to_uint16(), text);
    return written ? number(static_cast<int>(*written)) : null_reg;
}

// Saved games, by slot (1 to 99) and virtual id (100 + slot).

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
    const std::string description = machine.segments().get_string(args[2]);
    const int virtual_id = args[1].to_sint16();
    const std::vector<SaveDesc> saves = list_saves(machine);
    int slot = -1;
    if (virtual_id >= save_id_first && virtual_id <= save_id_last) {
        // An existing save is overwritten: it must be there.
        slot = virtual_id - save_id_first;
        if (find_save(saves, slot) == nullptr) {
            return null_reg;
        }
    } else if (virtual_id < save_id_first) {
        if (virtual_id == machine.last_save_virtual_id()) {
            // The same new save again overwrites the last one.
            slot = machine.last_save_new_id();
        } else {
            // A new save goes into the first slot without one.
            for (slot = save_slot_first; slot <= save_slot_last; ++slot) {
                if (find_save(saves, slot) == nullptr) {
                    break;
                }
            }
            if (slot > save_slot_last) {
                machine.faults().raise("kSavegame: no more savegame slots available");
                return null_reg;
            }
        }
    } else {
        machine.faults().raise("kSaveGame: invalid savegameId used");
        return null_reg;
    }
    machine.set_last_save(virtual_id, slot);

    SaveStore* store = files(machine).saves();
    if (store == nullptr) {
        warn("kSaveGame: there is no save folder; the game is not saved");
        return null_reg;
    }
    const std::vector<std::uint8_t> file = write_save(machine, description, files(machine).program());
    if (!store->write(slot, file)) {
        warn(std::format("kSaveGame: the save of slot {} could not be written", slot));
        return null_reg;
    }
    return true_reg;
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
    const int slot = id - save_id_first;
    const std::vector<SaveDesc> saves = list_saves(machine);
    SaveStore* store = files(machine).saves();
    const auto file = store != nullptr && find_save(saves, slot) != nullptr ? store->read(slot) : std::nullopt;
    if (!file) {
        warn(std::format("Savegame ID {} not found", slot));
        return true_reg; // Failure.
    }
    // The checks up to the game identity change nothing: the game is told
    // of the failure and runs on (gamestate_restore shows the message).
    const core::Sha256Digest game = game_identity(machine.resources().manager());
    if (const auto header = read_save_header(*file, &game); !header) {
        warn(std::format("Savegame ID {} cannot be restored: {}", slot, header.error().message));
        return true_reg;
    }
    // Past them, a failure may leave parts of the state replaced. The
    // state brings the saved clock, random source and music timer count;
    // the host's go on afterwards.
    const Subsystems::HostState host = machine.subsystems().host_state();
    if (const auto restored = read_save(machine, *file); !restored) {
        machine.faults().raise(std::format("kRestoreGame: {}", restored.error().message));
        return true_reg;
    }
    after_restore(machine, host);
    return null_reg; // Success.
}

Reg k_check_save_game(Machine& machine, std::span<Reg> args) {
    const std::uint16_t virtual_id = args[1].to_uint16();
    const std::vector<SaveDesc> saves = list_saves(machine);
    if (virtual_id == 0) {
        return null_reg;
    }
    if (virtual_id < save_id_first || virtual_id > save_id_last) {
        machine.faults().raise(std::format("kCheckSaveGame: called with invalid savegame ID ({})", virtual_id));
        return null_reg;
    }
    const SaveDesc* save = find_save(saves, virtual_id - save_id_first);
    if (save == nullptr) {
        return null_reg;
    }
    // Another format version or another release of the game: the game's
    // dialog says it cannot be restored (ScummVM checks the version here and
    // the release in kRestoreGame).
    return save->compatible ? true_reg : null_reg;
}

Reg k_get_save_files(Machine& machine, std::span<Reg> args) {
    // After the list, a low virtual id asks for a new slot.
    machine.set_last_save(save_id_first, machine.last_save_new_id());
    const std::vector<SaveDesc> saves = list_saves(machine);
    std::size_t total = std::min(saves.size(), max_listed_saves);
    const std::span<Reg> slots = machine.segments().deref_regs(args[2], static_cast<int>(total));
    if (slots.empty() && total > 0) {
        warn(std::format("kGetSaveFiles: {:04x}:{:04x} invalid or too small to hold slot data", args[2].segment,
                         args[2].offset));
        total = 0;
    }
    // The names, max_save_name_length bytes each, then a terminating zero.
    std::vector<std::uint8_t> names((total * max_save_name_length) + 1, 0);
    for (std::size_t i = 0; i < total; ++i) {
        slots[i] = number(saves[i].slot + save_id_first);
        const std::string& name = saves[i].name;
        std::copy_n(name.begin(), std::min(name.size(), max_save_name_length - 1),
                    names.begin() + static_cast<std::ptrdiff_t>(i * max_save_name_length));
    }
    machine.segments().memcpy(args[1], names);
    return number(static_cast<int>(total));
}

} // namespace sci::vm
