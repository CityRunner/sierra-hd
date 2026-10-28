// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project's save format (milestone 8, docs/formats/save-format.md): a
// header (magic, format version, game identity, description, date and
// time, play time, program version), the VM state as write_state writes
// it, and a SHA-256 checksum of everything before it.

#pragma once

#include "sci/core/error.hpp"
#include "sci/core/sha256.hpp"
#include "sci/vm/local_time.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sci::resource {
class ResourceManager;
}

namespace sci::vm {

class Machine;

inline constexpr std::string_view save_magic = "SCIHDSAV";
inline constexpr std::uint32_t save_format_version = 1;

struct SaveHeader {
    std::uint32_t version = save_format_version;
    // game_identity() of the game files the save was made with.
    core::Sha256Digest game{};
    // What the player typed in the game's save dialog.
    std::string description;
    // The date and time of the save (the clock's local time: the host's in
    // the program, the deterministic one in sci-vm-trace).
    LocalTime saved_at;
    // Virtual time at the save, in game ticks.
    std::uint32_t ticks = 0;
    // The program that wrote it.
    std::string program;
};

// The game identity: the SHA-256 of every script and vocabulary resource
// (type, number, length, bytes, in resource order), which is what the
// state's meaning depends on. Another release, or patched scripts, give
// another identity.
[[nodiscard]] core::Sha256Digest game_identity(const resource::ResourceManager& resources);

// The machine's state as a save file. `program` names the program version.
[[nodiscard]] std::vector<std::uint8_t> write_save(Machine& machine, std::string_view description,
                                                   std::string_view program);

// The header of a save file after checking the magic, the format version,
// the checksum and, unless `game` is null, the game identity. Errors are
// InvalidData (damaged or not a save), Unsupported (another format version
// or another game), with a message for the player.
[[nodiscard]] core::Result<SaveHeader> read_save_header(std::span<const std::uint8_t> file,
                                                        const core::Sha256Digest* game);

// Restores a save into a machine that is attached to its subsystems and
// initialised (init_game) on the same game files. On an InvalidData error
// after the header was accepted, the machine's state is undefined and it
// must not run on.
[[nodiscard]] core::Result<SaveHeader> read_save(Machine& machine, std::span<const std::uint8_t> file);

} // namespace sci::vm
