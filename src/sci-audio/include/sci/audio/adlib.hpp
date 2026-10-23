// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 AdLib driver: turns the music player's MIDI messages into OPL
// register writes, with the instrument patches of the game (patch.003),
// nine voices mapped to MIDI channels by the sound resource's header, and
// stereo panning on a dual OPL2 as in ScummVM. Integer-only and
// deterministic; the registers are written into an Opl3 the driver owns.
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/sound/drivers/adlib.cpp (MidiDriver_AdLib and
//     MidiPlayer_AdLib, the SCI0-late paths),
//   audio/softsynth/opl/nuked.cpp (OPL::init, OPL::write and
//     OPL::dualWrite: a dual OPL2 on one OPL3)
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

#pragma once

#include "sci/audio/midi_device.hpp"
#include "sci/audio/opl3.hpp"
#include "sci/core/error.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sci::resource {
class ResourceManager;
}

namespace sci::audio {

struct AdlibOperator {
    bool amplitude_mod = false;
    bool vibrato = false;
    bool envelope_type = false;
    bool kb_scale_rate = false;
    std::uint8_t frequency_mult = 0; // 0-15
    std::uint8_t kb_scale_level = 0; // 0-3
    std::uint8_t total_level = 0;    // 0-63, 0 is the loudest
    std::uint8_t attack_rate = 0;    // 0-15
    std::uint8_t decay_rate = 0;     // 0-15
    std::uint8_t sustain_level = 0;  // 0-15
    std::uint8_t release_rate = 0;   // 0-15
    std::uint8_t wave_form = 0;      // 0-3
};

struct AdlibPatch {
    std::array<AdlibOperator, 2> op{};
    std::uint8_t feedback = 0; // 0-7
    bool algorithm = false;    // true: additive (AM), both operators sound
};

// MidiDriver_AdLib::loadResource: 48 instruments of 28 bytes (1344 bytes,
// padded with 48 silent ones) or 96 (2690 bytes, two bytes between the
// halves). The SCI1.1 format (5382 bytes, with a rhythm key map) is not
// SCI0 and is refused as unsupported.
[[nodiscard]] core::Result<std::vector<AdlibPatch>> parse_adlib_patches(std::span<const std::uint8_t> data);

// MidiPlayer_AdLib::open: the patches from patch resource 3, or, without
// it, from the bank in ADL.DRV of the early SCI0 drivers (5684, 5720 or
// 5727 bytes, at 0x45A). Neither: NotFound, where ScummVM fails with
// "Failed to initialize sound driver".
[[nodiscard]] core::Result<std::vector<AdlibPatch>> load_adlib_patches(const resource::ResourceManager& resources);

class AdlibDriver final : public MidiDevice {
public:
    // What the driver reports to the player (MidiPlayer_AdLib, SCI0 late).
    // They equal the silent driver's values in sci-vm, so the player plays
    // the same with and without the device.
    static constexpr std::uint8_t play_id = 0x04;
    static constexpr int polyphony = 9;
    static constexpr bool has_rhythm_channel = false;
    // The OPL callback rate, which runs the music timer.
    static constexpr std::uint32_t timer_frequency = 250;
    static constexpr std::uint32_t base_tempo = 1000000 / timer_frequency;

    static constexpr int voice_count = 9;
    static constexpr int channel_count = 16;

    // One register write as it reaches the chip: bank 0 (0x000-0x0FF) is the
    // left OPL2, bank 1 (0x100-0x1FF) the right one.
    struct RegisterWrite {
        std::uint16_t reg;
        std::uint8_t value;

        friend bool operator==(const RegisterWrite&, const RegisterWrite&) = default;
    };

    // MidiDriver_AdLib::openAdLib: resets the chip into dual OPL2 mode and
    // writes the driver's start-up registers.
    explicit AdlibDriver(std::vector<AdlibPatch> patches);
    ~AdlibDriver() override = default;
    AdlibDriver(const AdlibDriver&) = delete;
    AdlibDriver& operator=(const AdlibDriver&) = delete;
    AdlibDriver(AdlibDriver&&) = delete;
    AdlibDriver& operator=(AdlibDriver&&) = delete;

    void send(std::uint32_t message) override;
    void init_track(std::span<const std::uint8_t> header) override;
    void set_volume(std::uint8_t volume) override;
    void play_switch(bool play) override;
    void on_timer() override;

    // The chip, whose generate() gives the output at Opl3::native_rate.
    [[nodiscard]] Opl3& opl() { return opl_; }

    // Every register write from now on is also appended to `log` (null
    // stops it). For tests.
    void set_register_log(std::vector<RegisterWrite>* log) { register_log_ = log; }

private:
    enum Side : std::uint8_t { left_side = 1, right_side = 2, both_sides = left_side | right_side };

    struct Channel {
        std::uint8_t patch = 0;
        std::uint8_t volume = 63;     // 0-63
        std::uint8_t pan = 64;        // 0-127, 64 is the centre
        std::uint8_t hold_pedal = 0;  // Any non-zero value holds.
        std::uint16_t pitch_wheel = 8192;
        std::uint8_t last_voice = 0;
        bool enable_velocity = false; // Controller 0x4E (SCI0).
        std::uint8_t voices = 0;      // Notes playing; wraps as in ScummVM.
        std::uint8_t mapped_voices = 0;
    };

    struct Voice {
        int channel = -1;        // The MIDI channel playing on the voice.
        int mapped_channel = -1; // The MIDI channel the voice is assigned to.
        int note = -1;
        int patch = -1;
        std::uint8_t velocity = 0;
        bool is_sustained = false;
        std::uint16_t age = 0;
    };

    void voice_on(int voice, int note, int velocity);
    void voice_off(int voice);
    void set_patch(int voice, int patch);
    void set_note(int voice, int note, bool key);
    void set_velocity(int voice);
    void set_operator(int reg, const AdlibOperator& op);
    void set_register(int reg, int value, Side sides = both_sides);
    void dual_write(int index, int reg, int value);
    void renew_notes(int channel, bool key);
    void note_on(int channel, int note, int velocity);
    void note_off(int channel, int note);
    int find_voice(int channel);
    void voice_mapping(int channel, int voices);
    void assign_voices(int channel, int voices);
    void release_voices(int channel, int voices);
    void set_velocity_reg(int reg_offset, int velocity, int kb_scale_level, int pan);
    [[nodiscard]] int calc_velocity(int voice, int op) const;

    Opl3 opl_;
    std::vector<AdlibPatch> patches_;
    std::array<Channel, channel_count> channels_{};
    std::array<Voice, voice_count> voices_{};
    bool play_switch_ = true;
    int master_volume_ = 15;
    std::vector<RegisterWrite>* register_log_ = nullptr;
};

} // namespace sci::audio
