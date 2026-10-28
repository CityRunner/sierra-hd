// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 AdLib driver (sci::audio::AdlibDriver). The expected register
// writes follow ScummVM's MidiDriver_AdLib (engines/sci/sound/drivers/
// adlib.cpp) on a dual OPL2: every write of a register goes to the left
// chip (bank 0) and then the right one (bank 1), except the level registers
// of the panned velocity, which are written per side.

#include "sci/audio/adlib.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using sci::audio::AdlibDriver;
using sci::audio::AdlibPatch;
using sci::audio::parse_adlib_patches;
using Write = AdlibDriver::RegisterWrite;

namespace {

// One instrument of patch.003: two operators of 13 bytes (key scale level,
// multiplier, feedback, attack, sustain, envelope type, decay, release,
// total level, AM, vibrato, KSR, inverted algorithm), then the waveforms.
constexpr std::array<std::uint8_t, 28> test_instrument{
    1, 2, 3, 4, 5,  1, 6,  7,  8,  1, 0, 1, 1, // Operator 0 (modulator); FM.
    2, 3, 0, 9, 10, 0, 11, 12, 20, 0, 1, 0, 0, // Operator 1 (carrier).
    1, 2,
};

// A 48-instrument bank whose instrument 0 is test_instrument; instrument 1 is
// the same in additive (AM) mode.
std::vector<AdlibPatch> test_patches() {
    std::vector<std::uint8_t> bank(1344, 0);
    std::ranges::copy(test_instrument, bank.begin());
    std::ranges::copy(test_instrument, bank.begin() + 28);
    bank[28 + 12] = 0;
    auto patches = parse_adlib_patches(bank);
    SCI_CHECK(patches.has_value());
    return patches ? std::move(*patches) : std::vector<AdlibPatch>(96);
}

// An SCI0-late header giving MIDI channel `channel` `voices` AdLib voices.
std::array<std::uint8_t, 0x21> header(int channel, std::uint8_t voices) {
    std::array<std::uint8_t, 0x21> data{};
    data[1 + (static_cast<std::size_t>(channel) * 2)] = voices;
    data[2 + (static_cast<std::size_t>(channel) * 2)] = 0x04;
    return data;
}

std::uint32_t midi(std::uint8_t status, std::uint8_t first, std::uint8_t second = 0) {
    return status | (static_cast<std::uint32_t>(first) << 8U) | (static_cast<std::uint32_t>(second) << 16U);
}

// A pitch wheel message on channel 0.
std::uint32_t wheel(unsigned value) {
    return midi(0xE0, static_cast<std::uint8_t>(value & 0x7FU), static_cast<std::uint8_t>(value >> 7U));
}

struct Driver {
    std::unique_ptr<AdlibDriver> driver = std::make_unique<AdlibDriver>(test_patches());
    std::vector<Write> log;

    explicit Driver(int voices = 1) {
        driver->init_track(header(0, static_cast<std::uint8_t>(voices)));
        driver->set_register_log(&log);
    }

    // The register writes of `message` alone.
    std::vector<Write> send(std::uint32_t message) {
        log.clear();
        driver->send(message);
        return log;
    }
};

// Both sides of a register, left first.
void both(std::vector<Write>& out, std::uint16_t reg, std::uint8_t value) {
    out.push_back({.reg = reg, .value = value});
    out.push_back({.reg = static_cast<std::uint16_t>(reg + 0x100), .value = value});
}

// The key-on events in a log: (voice, key bit) of each left B0-B8 write.
std::vector<std::pair<int, bool>> keys(const std::vector<Write>& log) {
    std::vector<std::pair<int, bool>> out;
    for (const Write& write : log) {
        if (write.reg >= 0xB0 && write.reg <= 0xB8) {
            out.emplace_back(write.reg - 0xB0, (write.value & 0x20U) != 0);
        }
    }
    return out;
}

// The left and right values of the last write to level register `reg`.
std::pair<int, int> levels(const std::vector<Write>& log, std::uint16_t reg) {
    int left = -1;
    int right = -1;
    for (const Write& write : log) {
        if (write.reg == reg) {
            left = write.value;
        } else if (write.reg == reg + 0x100) {
            right = write.value;
        }
    }
    return {left, right};
}

} // namespace

SCI_TEST(the_driver_starts_the_chip_as_a_dual_opl2) {
    // MidiDriver_AdLib::openAdLib after OPL::init (0x105 = 1, written at once).
    std::vector<Write> log;
    const auto driver = std::make_unique<AdlibDriver>(test_patches());
    driver->set_register_log(&log);
    driver->play_switch(true); // No notes: no writes.
    SCI_CHECK(log.empty());
}

SCI_TEST(the_write_hook_sees_the_driver_writes_before_the_dual_opl2_masks) {
    // As ScummVM's MidiDriver_AdLib::setRegister sends them to the ports
    // (the OPL logs compared with the reference): the start-up writes too,
    // and the feedback register without the output bits.
    std::vector<Write> hooked;
    const auto driver = std::make_unique<AdlibDriver>(
        test_patches(), [&](std::uint16_t reg, std::uint8_t value) { hooked.push_back({.reg = reg, .value = value}); });
    std::vector<Write> expected;
    both(expected, 0xBD, 0x00);
    both(expected, 0x08, 0x00);
    both(expected, 0x01, 0x20);
    SCI_CHECK(hooked == expected);
    std::vector<Write> log;
    driver->set_register_log(&log);
    hooked.clear();
    driver->init_track(header(0, 1));
    driver->send(midi(0xC0, 0));
    driver->send(midi(0x90, 60, 127));
    SCI_CHECK(hooked.size() == log.size());
    const auto feedback = std::ranges::find(hooked, Write{.reg = 0xC0, .value = 0x06});
    SCI_CHECK(feedback != hooked.end() && feedback + 1 != hooked.end() &&
              feedback[1] == Write{.reg = 0x1C0, .value = 0x06});
    driver->set_write_hook({});
    hooked.clear();
    driver->send(midi(0x80, 60));
    SCI_CHECK(hooked.empty());
}

SCI_TEST(note_on_programs_the_patch_frequency_and_level) {
    Driver adlib;
    SCI_CHECK(adlib.send(midi(0xC0, 0)).empty()); // Program change: state only.
    const std::vector<Write> writes = adlib.send(midi(0x90, 60, 127));
    std::vector<Write> expected;
    // setPatch(voice 0, patch 0): the modulator (offset 0), the carrier
    // (offset 3), then feedback and algorithm with each half's panning.
    both(expected, 0x40, 0x48);
    both(expected, 0x60, 0x46);
    both(expected, 0x80, 0x57);
    both(expected, 0x20, 0xB2);
    both(expected, 0xE0, 0x01);
    both(expected, 0x43, 0x94);
    both(expected, 0x63, 0x9B);
    both(expected, 0x83, 0xAC);
    both(expected, 0x23, 0x43);
    both(expected, 0xE3, 0x02);
    expected.push_back({.reg = 0xC0, .value = 0x56});
    expected.push_back({.reg = 0x1C0, .value = 0xA6});
    // setNote(60): index 240, frequency 0x157, octave 4, key on.
    both(expected, 0xA0, 0x57);
    both(expected, 0xB0, 0x31);
    // setVelocity: carrier level 15 * (63 - 20) / 15 = 43, so 63 - 43 = 20.
    both(expected, 0x43, 0x94);
    SCI_CHECK(writes == expected);
}

SCI_TEST(note_off_keys_off_the_voice) {
    Driver adlib;
    adlib.send(midi(0xC0, 0));
    adlib.send(midi(0x90, 60, 127));
    std::vector<Write> expected;
    both(expected, 0xA0, 0x57);
    both(expected, 0xB0, 0x11);
    both(expected, 0x43, 0x94);
    SCI_CHECK(adlib.send(midi(0x80, 60)) == expected);
    // Nothing plays: a second note off writes nothing.
    SCI_CHECK(adlib.send(midi(0x80, 60)).empty());
    // A note on a channel without voices, and an unplayable note, neither.
    SCI_CHECK(adlib.send(midi(0x91, 60, 127)).empty());
    SCI_CHECK(adlib.send(midi(0x90, 11, 127)).empty());
}

SCI_TEST(additive_patches_set_both_operator_levels) {
    Driver adlib;
    adlib.send(midi(0xC0, 1));
    const std::vector<Write> writes = adlib.send(midi(0x90, 60, 127));
    // The last writes: carrier level, then modulator level 63 - 8 = 55 -> 8.
    SCI_CHECK(writes.size() >= 4);
    const std::vector<Write> tail(writes.end() - 4, writes.end());
    std::vector<Write> expected;
    both(expected, 0x43, 0x94);
    both(expected, 0x40, 0x48);
    SCI_CHECK(tail == expected);
}

SCI_TEST(more_notes_than_voices_take_the_oldest_after_a_timer_call) {
    Driver adlib(2); // Voices 0 and 1 for channel 0.
    adlib.send(midi(0xC0, 0));
    // Round robin from the channel's last voice (0): voice 1, then voice 0.
    SCI_CHECK((keys(adlib.send(midi(0x90, 60, 100))) == std::vector<std::pair<int, bool>>{{1, true}}));
    SCI_CHECK((keys(adlib.send(midi(0x90, 62, 100))) == std::vector<std::pair<int, bool>>{{0, true}}));
    // A third note in the same timer period finds no voice old enough.
    SCI_CHECK(adlib.send(midi(0x90, 64, 100)).empty());
    adlib.driver->on_timer();
    // Now the oldest (ties: the last one searched, voice 0) is cut.
    SCI_CHECK((keys(adlib.send(midi(0x90, 64, 100))) == std::vector<std::pair<int, bool>>{{0, false}, {0, true}}));
    // The same note again restarts its voice.
    SCI_CHECK((keys(adlib.send(midi(0x90, 64, 100))) == std::vector<std::pair<int, bool>>{{0, false}, {0, true}}));
}

SCI_TEST(voice_mapping_releases_free_voices_first) {
    Driver adlib(2);
    adlib.send(midi(0xC0, 0));
    adlib.send(midi(0x90, 60, 100)); // On voice 1.
    // Controller 0x4B: one voice. Voice 0 has no note and goes first.
    SCI_CHECK(adlib.send(midi(0xB0, 0x4B, 1)).empty());
    adlib.driver->on_timer();
    // Voice 1 is the only one left: a new note cuts the playing one.
    SCI_CHECK((keys(adlib.send(midi(0x90, 62, 100))) == std::vector<std::pair<int, bool>>{{1, false}, {1, true}}));
    // Zero voices: the playing voice is keyed off and released.
    SCI_CHECK((keys(adlib.send(midi(0xB0, 0x4B, 0))) == std::vector<std::pair<int, bool>>{{1, false}}));
    SCI_CHECK(adlib.send(midi(0x90, 64, 100)).empty());
}

SCI_TEST(the_pitch_wheel_moves_the_playing_note) {
    Driver adlib;
    adlib.send(midi(0xC0, 0));
    adlib.send(midi(0x90, 60, 127));
    // 171 * 4 above the centre: one semitone, index 244, frequency 0x16B.
    std::vector<Write> writes = adlib.send(wheel(0x2000 + (171 * 4)));
    SCI_CHECK(writes.size() == 6);
    SCI_CHECK((writes[0] == Write{.reg = 0xA0, .value = 0x6B}));
    SCI_CHECK((writes[2] == Write{.reg = 0xB0, .value = 0x31}));
    // A semitone down: index 236, frequency 0x287 in octave 3.
    writes = adlib.send(wheel(0x2000 - (171 * 4)));
    SCI_CHECK((writes[0] == Write{.reg = 0xA0, .value = 0x87}));
    SCI_CHECK((writes[2] == Write{.reg = 0xB0, .value = 0x2E}));
    // Less than 171 from the centre does nothing to the frequency.
    writes = adlib.send(midi(0xE0, 0x00, 0x41));
    SCI_CHECK((writes[0] == Write{.reg = 0xA0, .value = 0x57}));
}

SCI_TEST(volume_velocity_and_pan_set_the_levels) {
    Driver adlib;
    adlib.send(midi(0xC0, 0));
    adlib.send(midi(0x90, 60, 127));
    // Master volume 7: (7 + 3) * 43 / 15 = 28, so level 63 - 28 = 35.
    adlib.log.clear();
    adlib.driver->set_volume(7);
    SCI_CHECK((levels(adlib.log, 0x43) == std::pair<int, int>{0x80 + 35, 0x80 + 35}));
    // Master volume 0 silences: level 63.
    adlib.log.clear();
    adlib.driver->set_volume(0);
    SCI_CHECK((levels(adlib.log, 0x43) == std::pair<int, int>{0x80 + 63, 0x80 + 63}));
    adlib.driver->set_volume(15);
    // Controller 0x4E: the note velocity (100 / 2 = 50) instead of the
    // instrument's level: 15 * 50 / 15 = 50, so level 13.
    adlib.send(midi(0xB0, 0x4E, 1));
    SCI_CHECK((levels(adlib.send(midi(0x90, 60, 100)), 0x43) == std::pair<int, int>{0x80 + 13, 0x80 + 13}));
    // Pan right: the left side goes silent; pan left: the right side.
    SCI_CHECK((levels(adlib.send(midi(0xB0, 0x0A, 127)), 0x43) == std::pair<int, int>{0x80 + 63, 0x80 + 13}));
    SCI_CHECK((levels(adlib.send(midi(0xB0, 0x0A, 0)), 0x43) == std::pair<int, int>{0x80 + 13, 0x80 + 63}));
    // Pan 32: the right side at 50 * 32 / 64 = 25, level 38.
    SCI_CHECK((levels(adlib.send(midi(0xB0, 0x0A, 32)), 0x43) == std::pair<int, int>{0x80 + 13, 0x80 + 38}));
    // The play switch off zeroes the levels and keys off.
    adlib.log.clear();
    adlib.driver->play_switch(false);
    SCI_CHECK((levels(adlib.log, 0x43) == std::pair<int, int>{0x80 + 63, 0x80 + 63}));
    SCI_CHECK((keys(adlib.log) == std::vector<std::pair<int, bool>>{{0, false}}));
}

SCI_TEST(the_hold_pedal_sustains_until_released) {
    Driver adlib;
    adlib.send(midi(0xC0, 0));
    adlib.send(midi(0xB0, 0x40, 127));
    adlib.send(midi(0x90, 60, 127));
    SCI_CHECK(adlib.send(midi(0x80, 60)).empty());
    SCI_CHECK((keys(adlib.send(midi(0xB0, 0x40, 0))) == std::vector<std::pair<int, bool>>{{0, false}}));
    // All notes off (controller 0x7B).
    adlib.send(midi(0x90, 62, 127));
    SCI_CHECK((keys(adlib.send(midi(0xB0, 0x7B, 0))) == std::vector<std::pair<int, bool>>{{0, false}}));
}

SCI_TEST(patch_banks_of_48_and_96_instruments_load) {
    SCI_CHECK(!parse_adlib_patches(std::vector<std::uint8_t>(1000)).has_value());
    // SCI1.1 banks (with the rhythm key map) are not SCI0.
    SCI_CHECK(!parse_adlib_patches(std::vector<std::uint8_t>(5382)).has_value());
    const auto small = parse_adlib_patches(std::vector<std::uint8_t>(1344, 0xFF));
    SCI_CHECK(small.has_value() && small->size() == 96);
    if (small) {
        SCI_CHECK((*small)[47].op[0].total_level == 0x3F && (*small)[47].feedback == 7);
        SCI_CHECK((*small)[48].op[0].total_level == 0 && (*small)[48].algorithm); // A dummy.
    }
    // 96 instruments: the second half starts 2 bytes after the first.
    std::vector<std::uint8_t> large(2690, 0);
    large[(48 * 28) + 2 + 8] = 0x15; // Instrument 48, operator 0 total level.
    const auto bank = parse_adlib_patches(large);
    SCI_CHECK(bank.has_value() && bank->size() == 96);
    if (bank) {
        SCI_CHECK((*bank)[48].op[0].total_level == 0x15);
    }
}
