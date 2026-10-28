// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 MT-32 driver: sends the game's MT-32 patch (patch.001: display
// texts, master volume, patches, timbres, rhythm setup, partial reserve and
// reverb settings) to an emulated MT-32 when it opens, assigns its parts to
// the MIDI channels of each song (initTrack) and passes the player's MIDI
// on with the driver's volume, program and rhythm handling. Ported from
// ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/sound/drivers/midi.cpp (MidiPlayer_Midi: the SCI0-late
//     paths for an emulated MT-32; readMt32Patch, sendMt32SysEx,
//     resetMt32, setMt32Volume, initTrack, noteOn, controlChange,
//     setPatch, send, setVolume, setReverb, playSwitch, getPlayId),
//   audio/softsynth/mt32.cpp (MidiDriver_MT32::sysEx: the driver's SysEx
//     messages written straight into the synthesiser's memory)
// and translated into this project's style, types and error handling. The
// General MIDI mapping, the D-110 and the early SCI0 MT32.DRV bank are not
// ported (docs/ASSUMPTIONS.md, "Milestone 7: the MT-32 driver").
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
#include "sci/audio/mt32.hpp"
#include "sci/core/error.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace sci::resource {
class ResourceManager;
}

namespace sci::audio {

// An SCI MT-32 patch (patch.001), as MidiPlayer_Midi::readMt32Patch reads
// it. The display texts are 20 bytes each (the MT-32's display), not
// terminated.
struct Mt32Patch {
    static constexpr std::size_t text_size = 20;
    static constexpr std::size_t reverb_configs = 11;
    static constexpr std::size_t timbre_size = 246;

    using Text = std::array<std::uint8_t, text_size>;

    Text after_text{};  // Bytes 0-19, shown when the patch is sent.
    Text before_text{}; // Bytes 20-39, shown while it is sent.
    Text goodbye_text{};
    std::uint16_t volume = 0; // 0-100 once read (larger values are cut).
    std::int8_t default_reverb = -1;
    // The reverb mode, time and level of each of the 11 reverb settings
    // (stored by column in the file).
    std::array<std::array<std::uint8_t, 3>, reverb_configs> reverb{};
    std::array<std::uint8_t, 256> patches_1_48_low{}; // At 0x050000.
    std::array<std::uint8_t, 128> patches_1_48_high{};
    std::vector<std::array<std::uint8_t, timbre_size>> timbres; // At most 64.
    // After the 0xABCD flag.
    std::optional<std::array<std::uint8_t, 384>> patches_49_96;
    // After the 0xDCBA flag: the rhythm key map and the partial reserve.
    std::optional<std::array<std::uint8_t, 256>> rhythm_key_map;
    std::array<std::uint8_t, 9> partial_reserve{};
};

// MidiPlayer_Midi::isMt32GmPatch and readMt32Patch. A patch in the General
// MIDI format (a larger one, with MIDI data after its first 1155 bytes) is
// Unsupported; data that is neither, or shorter than its timbres, is
// InvalidData (ScummVM: "Failed to detect MT-32 patch format").
[[nodiscard]] core::Result<Mt32Patch> parse_mt32_patch(std::span<const std::uint8_t> data);

// The game's patch.001, or nothing when it has none (the driver then sends
// only the reset, as ScummVM does for an SCI0-late game without it).
[[nodiscard]] core::Result<std::optional<Mt32Patch>> load_mt32_patch(const resource::ResourceManager& resources);

class Mt32Driver final : public MidiDevice {
public:
    // What the driver reports to the player (MidiPlayer_Midi, SCI0, on an
    // MT-32).
    static constexpr std::uint8_t play_id = 0x01;
    static constexpr int polyphony = 32;
    static constexpr bool has_rhythm_channel = true;
    // ScummVM's emulated MIDI drivers call the music timer 250 times a
    // second (MidiDriver_Emulated::_baseFreq), as the OPL does.
    static constexpr std::uint32_t timer_frequency = 250;
    static constexpr std::uint32_t base_tempo = 1000000 / timer_frequency;
    static constexpr int rhythm_channel = 9;

    // The address and data of one SysEx message the driver sends, before
    // the checksum (for tests).
    using SysexHook = std::function<void(std::span<const std::uint8_t> address_and_data)>;

    // MidiPlayer_Midi::open on an emulated MT-32: resets the synthesiser
    // and sends `patch`, if any. `hook` (if any) sees every SysEx message
    // the driver sends, from the reset on.
    Mt32Driver(std::unique_ptr<Mt32> synth, const std::optional<Mt32Patch>& patch, SysexHook hook = {});
    // MidiPlayer_Midi::close: shows the goodbye text.
    ~Mt32Driver() override;
    Mt32Driver(const Mt32Driver&) = delete;
    Mt32Driver& operator=(const Mt32Driver&) = delete;
    Mt32Driver(Mt32Driver&&) = delete;
    Mt32Driver& operator=(Mt32Driver&&) = delete;

    [[nodiscard]] PlayerChoices player_choices() const override;
    void send(std::uint32_t message) override;
    void init_track(std::span<const std::uint8_t> header) override;
    void set_volume(std::uint8_t volume) override;
    void play_switch(bool play) override;
    void sysex(std::span<const std::uint8_t> message) override;
    void set_reverb(std::int8_t reverb) override;
    void on_timer() override {}

    // The synthesiser, whose render() gives the output at Mt32::native_rate.
    // Messages reach it at once (Mt32::play_now): rendered after a timer
    // call, they take effect at that call's frame.
    [[nodiscard]] Mt32& synth() { return *synth_; }

private:
    struct Channel {
        std::uint8_t mapped_patch = unmapped;
        std::uint8_t patch = unmapped;
        bool playing = false;
        std::uint8_t pan = 0x40;
        std::uint8_t hold = 0;
        std::uint8_t volume = 0x7F;
    };

    static constexpr std::uint8_t unmapped = 0xFF;

    void send_sysex(std::uint32_t address, std::span<const std::uint8_t> data);
    void send_patch(const Mt32Patch& patch);
    void note_on(int channel, int note, int velocity);
    void control_change(int channel, int control, int value);
    void set_patch(int channel, int patch);
    void send_midi(int status, int first, int second);

    std::unique_ptr<Mt32> synth_;
    SysexHook hook_;
    std::array<Channel, 16> channels_{};
    bool play_switch_ = true;
    int master_volume_ = 15;
    bool has_reverb_ = false;
    std::int8_t reverb_ = -1;
    std::int8_t default_reverb_ = -1;
    std::array<std::array<std::uint8_t, 3>, Mt32Patch::reverb_configs> reverb_config_{};
    Mt32Patch::Text goodbye_text_{};
};

} // namespace sci::audio
