// SPDX-License-Identifier: GPL-3.0-or-later
//
// kSaveGame, kGetSaveFiles, kCheckSaveGame and kRestoreGame on the save
// format (milestone 8, docs/formats/save-format.md, "Slots and the save
// folder") on a synthetic machine: slots, the list the game's dialogs read
// and its order, restoring, and the failures returned to the game.

#include "files.hpp"
#include "kernel_functions.hpp"
#include "machine.hpp"
#include "music.hpp"
#include "save_game.hpp"
#include "save_world.hpp"
#include "sci/core/sha256.hpp"
#include "sci/vm/files.hpp"
#include "sci_test.hpp"
#include "seg_manager.hpp"
#include "sound.hpp"
#include "state.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <utility>
#include <vector>

using sci::test::SaveWorld;
using sci::test::synthetic_game;
using sci::vm::Reg;

namespace {

constexpr std::size_t checksum_size = 32;

// A synthetic machine with saves in memory and buffers for the arguments:
// dynamic memory for the description and the names, the stack for the slot
// ids.
struct KernelWorld {
    SaveWorld world;
    Reg text;
    Reg names;
    Reg slots;

    explicit KernelWorld(sci::vm::SaveStore& saves, std::uint8_t marker = 0) : world(game(marker)) {
        world.populate();
        world.subsystems.files().set_saves(&saves, "test 1.0");
        world.machine->set_execution_stack_base(0);
        world.machine->faults().clear(); // The synthetic game has no vocabularies.
        sci::vm::SegManager& segments = world.machine->segments();
        (void)segments.allocate_dynmem(64, "text", text);
        (void)segments.allocate_dynmem((20 * 36) + 1, "names", names);
        slots = sci::vm::make_reg(segments.find_segment_by_type(sci::vm::SegmentType::Stack), 0);
    }

    static sci::resource::ResourceManager game(std::uint8_t marker) {
        auto manager = synthetic_game(marker);
        if (!manager) {
            std::abort(); // The synthetic folder always opens.
        }
        return std::move(*manager);
    }

    [[nodiscard]] sci::vm::Machine& machine() const { return *world.machine; }

    // The digest of the random source's part of the state.
    [[nodiscard]] sci::core::Sha256Digest random_state() const {
        for (const sci::vm::StateComponentHash& component : sci::vm::state_hash(machine()).components) {
            if (component.name == "random") {
                return component.digest;
            }
        }
        return {};
    }

    Reg save(int virtual_id, const std::string& description) {
        std::vector<std::uint8_t> bytes(description.begin(), description.end());
        bytes.push_back(0);
        machine().segments().memcpy(text, bytes);
        std::array<Reg, 3> args{text, sci::vm::number(virtual_id), text};
        return sci::vm::k_save_game(machine(), args);
    }

    // The names and virtual ids kGetSaveFiles lists.
    std::vector<std::pair<std::string, int>> list() {
        std::array<Reg, 3> args{text, names, slots};
        const Reg count = sci::vm::k_get_save_files(machine(), args);
        std::vector<std::pair<std::string, int>> result;
        const std::span<Reg> ids = machine().segments().deref_regs(slots, count.to_uint16());
        const std::size_t listed = count.to_uint16();
        for (std::size_t i = 0; i < listed && i < ids.size(); ++i) {
            const std::string name = machine().segments().get_string(
                sci::vm::make_reg(names.segment, static_cast<std::uint16_t>(names.offset + (i * 36))));
            result.emplace_back(name, ids[i].to_uint16());
        }
        return result;
    }

    bool check(int virtual_id) {
        std::array<Reg, 2> args{text, sci::vm::number(virtual_id)};
        return !sci::vm::k_check_save_game(machine(), args).is_null();
    }

    // True on success, as the game reads kRestoreGame's result.
    bool restore(int virtual_id) {
        std::array<Reg, 2> args{text, sci::vm::number(virtual_id)};
        return sci::vm::k_restore_game(machine(), args).is_null();
    }
};

// The file of a slot, or nothing.
std::vector<std::uint8_t> file_of(sci::vm::SaveStore& saves, int slot) {
    return saves.read(slot).value_or(std::vector<std::uint8_t>{});
}

void reseal(std::vector<std::uint8_t>& file) {
    file.resize(file.size() - checksum_size);
    const sci::core::Sha256Digest checksum = sci::core::sha256(file);
    file.insert(file.end(), checksum.begin(), checksum.end());
}

} // namespace

SCI_TEST(new_saves_take_free_slots_and_the_same_id_overwrites) {
    sci::vm::MemorySaveStore saves;
    KernelWorld game(saves);
    SCI_CHECK(game.list().empty());
    SCI_CHECK(!game.save(0, "First").is_null());
    SCI_CHECK(saves.slots() == std::vector<int>{1});
    SCI_CHECK(sci::vm::read_save_summary(file_of(saves, 1)).value_or(sci::vm::SaveHeader{}).description == "First");
    // The same low id again: the last new save is overwritten.
    SCI_CHECK(!game.save(0, "First again").is_null());
    SCI_CHECK(saves.slots() == std::vector<int>{1});
    // After the list, a low id asks for a new slot.
    SCI_CHECK(game.list().size() == 1);
    SCI_CHECK(!game.save(0, "Second").is_null());
    SCI_CHECK(saves.slots() == (std::vector<int>{1, 2}));
    // An existing slot by its virtual id; a missing one fails.
    SCI_CHECK(!game.save(101, "First, third time").is_null());
    SCI_CHECK(game.save(150, "Nowhere").is_null());
    SCI_CHECK(saves.slots() == (std::vector<int>{1, 2}));
}

SCI_TEST(the_list_is_newest_first_then_by_slot) {
    sci::vm::MemorySaveStore saves;
    KernelWorld game(saves);
    SCI_CHECK(!game.save(0, "Older").is_null());
    (void)game.list();
    SCI_CHECK(!game.save(0, "Same second").is_null());
    auto listed = game.list();
    SCI_CHECK(listed.size() == 2);
    SCI_CHECK(listed[0] == std::pair<std::string, int>("Older", 101));
    SCI_CHECK(listed[1] == std::pair<std::string, int>("Same second", 102));
    game.world.subsystems.clock().advance(2000);
    SCI_CHECK(!game.save(0, "Newest, with a description longer than thirty-five").is_null());
    listed = game.list();
    SCI_CHECK(listed.size() == 3);
    SCI_CHECK(listed[0].second == 103);
    SCI_CHECK(listed[0].first == std::string("Newest, with a description longer than thirty-five").substr(0, 35));
    SCI_CHECK(listed[1].second == 101);
    SCI_CHECK(listed[2].second == 102);
}

SCI_TEST(at_most_twenty_saves_are_listed) {
    sci::vm::MemorySaveStore saves;
    KernelWorld game(saves);
    for (int i = 0; i < 22; ++i) {
        (void)game.list();
        SCI_CHECK(!game.save(0, "Save " + std::to_string(i)).is_null());
    }
    SCI_CHECK(saves.slots().size() == 22);
    SCI_CHECK(game.list().size() == 20);
}

SCI_TEST(a_restore_brings_the_state_back_and_sets_the_restore_flag) {
    sci::vm::MemorySaveStore saves;
    KernelWorld game(saves);
    const std::array<std::uint8_t, 1> one{1};
    const std::array<std::uint8_t, 1> two{2};
    game.machine().segments().memcpy(game.names, one);
    sci::vm::Music& music = game.world.subsystems.sound().music();
    music.set_timer_calls(5);
    const Reg saved_strings = game.machine().segments().save_dir_pointer();
    const sci::core::Sha256Digest saved = game.random_state();
    SCI_CHECK(!game.save(0, "Before").is_null());
    game.machine().segments().memcpy(game.names, two);
    music.set_timer_calls(9);
    (void)game.world.subsystems.random().next(10);
    const sci::core::Sha256Digest host = game.random_state();
    SCI_CHECK(host != saved);
    SCI_CHECK(game.check(101));
    SCI_CHECK(game.restore(101));
    SCI_CHECK(!game.machine().faults().raised());
    SCI_CHECK(game.machine().segments().deref_bulk(game.names, 1)[0] == 1);
    // The random source is the host's (SciEngine's, not saved): it runs on.
    SCI_CHECK(game.random_state() == host);
    // So is the music timer's call count, the logs' time key.
    SCI_CHECK(music.timer_calls() == 9);
    // ScummVM neither saves nor loads the hunk table: the hunk segment is
    // gone until the next hunk, and the system strings are allocated again.
    SCI_CHECK(game.machine().segments().find_segment_by_type(sci::vm::SegmentType::Hunk) == 0);
    const Reg strings = game.machine().segments().save_dir_pointer();
    SCI_CHECK(strings.segment != saved_strings.segment);
    SCI_CHECK(game.machine().segments().dereference(strings).max_size >= 512);
    SCI_CHECK(game.machine().aborting() == sci::vm::Abort::LoadGame);
    SCI_CHECK(game.machine().game_is_restarting() == sci::vm::Restarting::Restore);
    // kGameIsRestarting reports 2, then clears it.
    std::array<Reg, 1> clear{sci::vm::number(0)};
    SCI_CHECK(sci::vm::k_game_is_restarting(game.machine(), clear) == sci::vm::number(2));
    SCI_CHECK(game.machine().game_is_restarting() == sci::vm::Restarting::None);
}

SCI_TEST(a_restore_keeps_the_host_time_and_sets_the_play_time_back) {
    sci::vm::MemorySaveStore saves;
    KernelWorld game(saves);
    sci::vm::Clock& clock = game.world.subsystems.clock();
    clock.advance(1000);
    SCI_CHECK(!game.save(0, "At one second").is_null());
    clock.advance(2000);
    clock.set_throttle_trigger(true);
    SCI_CHECK(clock.ticks() == 180 && clock.play_ticks() == 180);
    SCI_CHECK(game.restore(101));
    // Virtual time is the host's: it runs on. The play time (kGetTime) is
    // the save's again and runs on from there.
    SCI_CHECK(clock.ticks() == 180);
    SCI_CHECK(clock.play_ticks() == 60);
    std::array<Reg, 1> elapsed{sci::vm::number(0)};
    SCI_CHECK(sci::vm::k_get_time(game.machine(), elapsed) == sci::vm::number(60));
    // The last wait is now (lastWaitTime), and the throttle is reset.
    SCI_CHECK(!clock.throttle_trigger());
    SCI_CHECK(clock.wait(6) == 6);
    SCI_CHECK(clock.ticks() == 186 && clock.play_ticks() == 66);
}

SCI_TEST(missing_saves_are_not_found) {
    sci::vm::MemorySaveStore saves;
    KernelWorld game(saves);
    SCI_CHECK(!game.check(0));
    SCI_CHECK(!game.check(101));
    SCI_CHECK(!game.restore(101));
    SCI_CHECK(!game.restore(5)); // Not a virtual id.
    SCI_CHECK(game.machine().aborting() == sci::vm::Abort::None);
}

SCI_TEST(saves_of_another_release_version_or_damaged_fail_without_change) {
    sci::vm::MemorySaveStore saves;
    // Slot 1: another release of the game.
    {
        sci::vm::MemorySaveStore other_saves;
        KernelWorld other(other_saves, 1);
        SCI_CHECK(!other.save(0, "Other release").is_null());
        saves.write(1, file_of(other_saves, 1));
    }
    KernelWorld game(saves);
    (void)game.list();
    SCI_CHECK(!game.save(0, "Good").is_null()); // Slot 2.
    const std::vector<std::uint8_t> file = file_of(saves, 2);
    // Slot 3: format version 2.
    std::vector<std::uint8_t> newer = file;
    newer[8] = 2;
    reseal(newer);
    saves.write(3, newer);
    // Slot 4: a damaged byte (the checksum is wrong).
    std::vector<std::uint8_t> damaged = file;
    damaged[damaged.size() / 2] ^= 0xFFU;
    saves.write(4, damaged);
    // Slot 5: not a save at all: not listed.
    saves.write(5, std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'});

    SCI_CHECK(game.list().size() == 4);
    (void)game.world.subsystems.random().next(10); // Unlike in the saves.
    const sci::core::Sha256Digest before = game.random_state();
    for (const int id : {101, 103, 104}) {
        SCI_CHECK(!game.check(id));
        SCI_CHECK(!game.restore(id));
    }
    SCI_CHECK(!game.check(105));
    SCI_CHECK(!game.restore(105));
    SCI_CHECK(!game.machine().faults().raised());
    SCI_CHECK(game.machine().aborting() == sci::vm::Abort::None);
    SCI_CHECK(game.random_state() == before);
    SCI_CHECK(game.check(102));
}

SCI_TEST(an_inconsistent_state_stops_the_game) {
    sci::vm::MemorySaveStore saves;
    KernelWorld game(saves);
    SCI_CHECK(!game.save(0, "Good").is_null());
    std::vector<std::uint8_t> file = file_of(saves, 1);
    // Cut the state short inside its block: the header and identity pass.
    file[file.size() - checksum_size - 1] ^= 0xFFU;
    file.resize(file.size() - checksum_size - 40);
    reseal(file);
    saves.write(1, file);
    SCI_CHECK(game.check(101)); // The header is fine.
    SCI_CHECK(!game.restore(101));
    SCI_CHECK(game.machine().faults().raised());
}
