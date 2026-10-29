// SPDX-License-Identifier: GPL-3.0-or-later
//
// The VM state and its hash (milestone 8, docs/ASSUMPTIONS.md, "Milestone 8:
// the VM state"): on a machine over a synthetic resource folder, equal
// states hash equally whatever their addresses, and changing one thing
// changes the hash of its component and only that one.

#include "builders.hpp"
#include "events.hpp"
#include "gfx_ports.hpp"
#include "gfx_screen.hpp"
#include "graphics.hpp"
#include "machine.hpp"
#include "music.hpp"
#include "parser.hpp"
#include "sci/resource/resource_manager.hpp"
#include "sci_test.hpp"
#include "segments.hpp"
#include "sound.hpp"
#include "state.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using sci::resource::GameFile;
using sci::resource::ResourceManager;
using sci::resource::ResourceType;
using sci::vm::Machine;
using sci::vm::Reg;
using sci::vm::StateHash;
using sci::vm::Subsystems;

namespace {

// A folder with one text resource: enough for a machine to exist (it
// reports the missing vocabularies as faults, which the state ignores).
ResourceManager folder() {
    constexpr std::array<std::uint8_t, 1> text{'x'};
    sci::test::Bytes volume;
    sci::test::append_stored_entry(volume, ResourceType::Text, 1, text);
    sci::test::Bytes map;
    sci::test::append_map_entry(map, ResourceType::Text, 1, 1, 0);
    sci::test::append_map_terminator(map);
    std::vector<GameFile> files;
    files.push_back(GameFile{.name = "resource.map", .bytes = std::move(map)});
    files.push_back(GameFile{.name = "RESOURCE.001", .bytes = std::move(volume)});
    auto manager = ResourceManager::open(std::move(files));
    SCI_CHECK(manager.has_value());
    return std::move(*manager);
}

// A machine with its subsystems attached and some state of each kind: a
// locals segment, a list with a node, a hunk, a song.
struct World {
    ResourceManager resources = folder();
    Subsystems subsystems;
    std::unique_ptr<Machine> machine;
    sci::vm::SegmentId locals = 0;
    Reg list;
    Reg node;
    Reg hunk;

    World() {
        machine = std::make_unique<Machine>(resources, subsystems);
        subsystems.attach(*machine);
        sci::vm::SegManager& segments = machine->segments();
        auto block = std::make_unique<sci::vm::LocalVariables>();
        block->locals.assign(4, sci::vm::number(7));
        locals = segments.alloc_segment(std::move(block));
        segments.allocate_list(list);
        node = segments.new_node(sci::vm::number(1), sci::vm::number(2));
        segments.lookup_list(list)->first = node;
        segments.lookup_list(list)->last = node;
        hunk = segments.allocate_hunk_entry("test", 8);
        auto song = std::make_unique<sci::vm::MusicEntry>();
        song->sound_object = sci::vm::make_reg(5, 0x10);
        song->signal = 0;
        subsystems.sound().music().add(std::move(song));
    }

    [[nodiscard]] StateHash hash() const { return sci::vm::state_hash(*machine); }
};

std::set<std::string> changed(const StateHash& before, const StateHash& after) {
    std::set<std::string> names;
    SCI_CHECK(before.components.size() == after.components.size());
    for (std::size_t i = 0; i < before.components.size() && i < after.components.size(); ++i) {
        SCI_CHECK(before.components[i].name == after.components[i].name);
        if (before.components[i].digest != after.components[i].digest) {
            names.insert(before.components[i].name);
        }
    }
    return names;
}

// Changing one thing changes `component`, and the total; the persistent
// hash too unless the component is the execution.
void check_changes(const std::string& component, const std::function<void(World&)>& mutate) {
    World world;
    const StateHash before = world.hash();
    mutate(world);
    const StateHash after = world.hash();
    SCI_CHECK(changed(before, after) == std::set<std::string>{component});
    SCI_CHECK(before.total != after.total);
    SCI_CHECK((before.persistent != after.persistent) == (component != "execution"));
}

} // namespace

SCI_TEST(the_state_has_the_components_in_order) {
    const World world;
    const StateHash hash = world.hash();
    std::vector<std::string> names;
    for (const auto& component : hash.components) {
        names.push_back(component.name);
        SCI_CHECK(component.bytes > 0);
    }
    SCI_CHECK(names == (std::vector<std::string>{"execution", "segments", "machine", "clock", "random", "events",
                                                 "files", "parser", "sound", "graphics"}));
}

SCI_TEST(equal_states_hash_equally_at_other_addresses) {
    const World first;
    const auto padding = std::make_unique<std::array<std::uint8_t, 4096>>();
    const World second;
    SCI_CHECK(first.hash().total == second.hash().total);
    SCI_CHECK(first.hash().persistent == second.hash().persistent);
}

SCI_TEST(hashing_changes_nothing) {
    const World world;
    SCI_CHECK(world.hash().total == world.hash().total);
}

SCI_TEST(a_variable_changes_the_segments) {
    check_changes("segments", [](World& world) {
        auto* block = dynamic_cast<sci::vm::LocalVariables*>(
            world.machine->segments().segment(world.locals, sci::vm::SegmentType::Locals));
        block->locals[2] = sci::vm::number(8);
    });
}

SCI_TEST(a_list_node_changes_the_segments) {
    check_changes("segments",
                  [](World& world) { world.machine->segments().lookup_node(world.node)->value = sci::vm::number(3); });
}

SCI_TEST(a_freed_node_changes_the_segments) {
    check_changes("segments", [](World& world) {
        auto* nodes = dynamic_cast<sci::vm::NodeTable*>(
            world.machine->segments().segment(world.node.segment, sci::vm::SegmentType::Nodes));
        nodes->free_entry(world.node.offset);
    });
}

SCI_TEST(a_hunk_byte_changes_the_segments) {
    check_changes("segments", [](World& world) { world.machine->segments().hunk(world.hunk)->mem[3] = 9; });
}

SCI_TEST(the_accumulator_changes_only_the_execution) {
    check_changes("execution", [](World& world) { world.machine->set_acc(sci::vm::number(42)); });
}

SCI_TEST(the_clock_changes_the_clock) {
    // Without letting time pass, which would run the music timer.
    check_changes("clock", [](World& world) { world.subsystems.clock().read_elapsed(5); });
    check_changes("clock", [](World& world) { world.subsystems.clock().set_throttle_trigger(true); });
}

SCI_TEST(the_random_source_changes_the_random_state) {
    check_changes("random", [](World& world) { (void)world.subsystems.random().next(100); });
}

SCI_TEST(an_event_changes_the_events) {
    check_changes("events", [](World& world) {
        world.subsystems.events().push(
            sci::vm::SciEvent{.type = sci::vm::event_key_down, .modifiers = 0, .character = 'a', .mouse = {}});
    });
}

SCI_TEST(a_synonym_changes_the_parser) {
    check_changes("parser", [](World& world) { world.subsystems.parser().add_synonym(1, 2); });
}

SCI_TEST(a_song_signal_changes_the_sound) {
    check_changes("sound", [](World& world) { world.subsystems.sound().music().at(0).signal = 0xFFFF; });
}

SCI_TEST(a_port_changes_the_graphics) {
    check_changes("graphics", [](World& world) { world.subsystems.graphics().ports().pen_color(3); });
}

SCI_TEST(a_pixel_changes_the_graphics) {
    check_changes("graphics", [](World& world) {
        world.subsystems.graphics().screen().put_pixel(10, 20, sci::vm::mask_control, 0, 0, 5);
    });
}

SCI_TEST(the_reader_reads_what_the_writer_wrote) {
    sci::vm::StateWriter out;
    out.begin("one");
    out.u8(0xAB);
    out.boolean(true);
    out.u16(0x1234);
    out.u32(0x89ABCDEF);
    out.u64(0x0123456789ABCDEFULL);
    out.i16(-2);
    out.begin("two");
    out.reg(sci::vm::make_reg(3, 0x44));
    out.string("text");
    out.bytes(std::vector<std::uint8_t>{1, 2, 3});
    const auto components = out.components();
    SCI_CHECK(components.size() == 2);
    SCI_CHECK(components[0].name == "one" && components[1].name == "two");
    SCI_CHECK(components[0].end == components[1].begin && components[1].end == out.data().size());
    // Little-endian, whatever the host.
    SCI_CHECK(out.data()[components[0].begin + 4 + 3 + 2] == 0x34);

    sci::vm::StateReader in(out.data());
    SCI_CHECK(in.string() == "one");
    SCI_CHECK(in.u8() == 0xAB);
    SCI_CHECK(in.boolean());
    SCI_CHECK(in.u16() == 0x1234);
    SCI_CHECK(in.u32() == 0x89ABCDEF);
    SCI_CHECK(in.u64() == 0x0123456789ABCDEFULL);
    SCI_CHECK(in.i16() == -2);
    SCI_CHECK(in.string() == "two");
    SCI_CHECK(in.reg() == sci::vm::make_reg(3, 0x44));
    SCI_CHECK(in.string() == "text");
    SCI_CHECK(in.bytes() == (std::vector<std::uint8_t>{1, 2, 3}));
    SCI_CHECK(in.at_end() && !in.failed());
    SCI_CHECK(in.u8() == 0 && in.failed());
}
