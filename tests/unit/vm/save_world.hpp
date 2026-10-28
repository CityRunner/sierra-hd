// SPDX-License-Identifier: GPL-3.0-or-later
//
// A machine over a synthetic game folder that can be saved and restored
// (milestone 8, docs/formats/save-format.md): a script 0 with an export and
// four globals, the stack, and some state of each kind (a list with a node,
// a hunk, a song without a resource). No game data. Shared by the save
// tests and the save loader's fuzz target.

#pragma once

#include "builders.hpp"
#include "machine.hpp"
#include "music.hpp"
#include "parser.hpp"
#include "sci/resource/resource_manager.hpp"
#include "seg_manager.hpp"
#include "sound.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace sci::test {

// Script 0: an export table (export 0 at offset 0) and a block of four
// locals (the globals), then the terminator. `marker` changes the first
// local's initial value, making another "release" of the game.
inline Bytes synthetic_script0(std::uint8_t marker = 0) {
    // clang-format off
    return Bytes{
        7, 0, 8, 0, 1, 0, 0, 0,       // Exports: one, at 0.
        10, 0, 12, 0,                 // Locals: four words.
        marker, 0, 0, 0, 0, 0, 0, 0,
        0, 0,                         // Terminator.
    };
    // clang-format on
}

inline std::optional<resource::ResourceManager> synthetic_game(std::uint8_t marker = 0) {
    const Bytes script = synthetic_script0(marker);
    Bytes volume;
    append_stored_entry(volume, resource::ResourceType::Script, 0, script);
    Bytes map;
    append_map_entry(map, resource::ResourceType::Script, 0, 1, 0);
    append_map_terminator(map);
    std::vector<resource::GameFile> files;
    files.push_back(resource::GameFile{.name = "resource.map", .bytes = std::move(map)});
    files.push_back(resource::GameFile{.name = "RESOURCE.001", .bytes = std::move(volume)});
    auto manager = resource::ResourceManager::open(std::move(files));
    if (!manager) {
        return std::nullopt;
    }
    return std::move(*manager);
}

// The machine with script 0 loaded and the stack allocated, as init_game
// leaves them (the missing vocabularies are faults, which neither the
// state nor the loader looks at).
struct SaveWorld {
    resource::ResourceManager resources;
    vm::Subsystems subsystems;
    std::unique_ptr<vm::Machine> machine;

    explicit SaveWorld(resource::ResourceManager game) : resources(std::move(game)) {
        machine = std::make_unique<vm::Machine>(resources, subsystems);
        subsystems.attach(*machine);
        vm::SegManager& segments = machine->segments();
        (void)segments.script_segment(0, vm::ScriptLoad::Lock);
        (void)segments.allocate_stack(vm::vm_stack_size);
    }

    // Some state beyond what the machine starts with.
    void populate() {
        vm::SegManager& segments = machine->segments();
        vm::Reg list;
        segments.allocate_list(list);
        const vm::Reg node = segments.new_node(vm::number(1), vm::number(2));
        segments.lookup_list(list)->first = node;
        segments.lookup_list(list)->last = node;
        const vm::Reg hunk = segments.allocate_hunk_entry("test", 8);
        segments.hunk(hunk)->mem[2] = 7;
        auto song = std::make_unique<vm::MusicEntry>();
        song->sound_object = vm::make_reg(5, 0x10);
        subsystems.sound().music().add(std::move(song));
        subsystems.parser().add_synonym(3, 4);
        (void)subsystems.random().next(10);
    }
};

} // namespace sci::test
