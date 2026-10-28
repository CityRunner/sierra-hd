// SPDX-License-Identifier: GPL-3.0-or-later
//
// The music player of the SCI0 interpreter: the playlist of sounds, their
// priorities and states, and the music timer that runs their MIDI parsers.
// It plays through a silent stand-in for ScummVM's SCI0 AdLib driver, with
// that driver's play mask, voice count and timer rate, so timing, signals
// and cues are as in the reference (spec section 8); a device
// (sci::audio::MidiDevice, such as the AdLib driver) may be attached to
// hear it. The player asks the device for its play mask, rhythm channel
// and voice count, as ScummVM does: the AdLib's are the silent driver's,
// the MT-32's differ, which changes the channels played and the voices
// kDoSound GetPolyphony reports but neither the timing, signals nor cues
// (docs/ASSUMPTIONS.md, "Milestone 7: the MT-32 driver"). Ported from ScummVM
// (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/sound/music.{h,cpp} (SciMusic, MusicEntry; SCI0 paths),
//   engines/sci/resource/resource_audio.cpp (SoundResource, SCI0 late),
//   engines/sci/sound/drivers/adlib.cpp (MidiPlayer_AdLib constants)
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

#include "midi_parser.hpp"
#include "sci/audio/midi_device.hpp"
#include "sci/vm/reg.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sci::vm {

class StateWriter;
class StateReader;
class Faults;
class Resources;

enum class SoundStatus : std::uint8_t { Stopped = 0, Initialized = 1, Paused = 2, Playing = 3 };

// The signal of a sound that has ended.
inline constexpr std::uint16_t signal_offset = 0xFFFF;
inline constexpr int music_volume_max = 127;
inline constexpr int music_master_volume_max = 15;

// The silent driver: what ScummVM's SCI0 AdLib driver reports.
inline constexpr std::uint8_t adlib_play_id = 0x04; // SCI0 late.
inline constexpr int adlib_polyphony = 9;
inline constexpr bool adlib_has_rhythm_channel = false;
inline constexpr std::uint32_t adlib_base_tempo = 1000000 / 250; // OPL callback frequency.
inline constexpr audio::PlayerChoices silent_choices{
    .play_id = adlib_play_id,
    .polyphony = adlib_polyphony,
    .has_rhythm_channel = adlib_has_rhythm_channel,
};

// An SCI0 (late) sound resource: a 0x21-byte header, then one MIDI stream
// whose channels carry play masks per device, and optionally a digital
// sample after the stream's end.
struct SoundResource {
    std::span<const std::uint8_t> data;
    std::span<const std::uint8_t> song;
    bool has_digital_sample = false;

    // Null if the data is too short for the header.
    [[nodiscard]] static std::optional<SoundResource> parse(std::span<const std::uint8_t> data, int number);
    // SoundResource::getChannelFilterMask.
    [[nodiscard]] unsigned channel_filter_mask(unsigned hardware_mask, bool wants_rhythm) const;
};

struct MusicEntry {
    Reg sound_object;
    std::optional<SoundResource> resource;
    std::uint16_t resource_id = 0;
    int time = 0; // The order songs were added in, for priorities.
    std::uint16_t data_inc = 0;
    std::uint16_t ticker = 0;
    std::uint16_t signal = 0;
    std::int16_t priority = 0;
    std::uint16_t loop = 0;
    std::int16_t volume = music_volume_max;
    std::int16_t hold = -1;
    std::int8_t reverb = -1;
    bool play_bed = false;
    bool override_priority = false;
    std::int16_t pause_counter = 0;
    std::uint8_t fade_to = 0;
    std::int16_t fade_step = 0;
    std::uint32_t fade_ticker = 0;
    std::uint32_t fade_ticker_step = 0;
    bool fade_set_volume = false;
    bool fade_completed = false;
    bool stop_after_fading = false;
    SoundStatus status = SoundStatus::Stopped;
    std::unique_ptr<MidiParser> parser;
    // SCI0 signals not yet sent to the scripts.
    std::vector<std::uint16_t> signal_queue;

    void on_timer();
    void do_fade();
    void set_signal(int new_signal);
};

class Music {
public:
    explicit Music(Faults& faults);

    // SciMusic::onTimer, once per adlib_base_tempo microseconds.
    void on_timer();

    // The music timer calls made so far: the time key of the OPL logs.
    [[nodiscard]] std::uint32_t timer_calls() const { return timer_calls_; }

    void clear_play_list();
    void stop_all();

    void sound_init(MusicEntry& sound);
    void sound_play(MusicEntry& sound, bool restoring = false);
    void sound_stop(MusicEntry& sound);
    void sound_kill(MusicEntry& sound);
    void sound_pause(MusicEntry& sound);
    void sound_resume(MusicEntry& sound);
    // SciMusic::pauseAll: pauses or resumes every song, with a counter.
    void pause_all(bool pause);
    void sound_set_volume(MusicEntry& sound, int volume);
    void sound_set_priority(MusicEntry& sound, std::uint8_t priority);

    [[nodiscard]] std::uint16_t master_volume() const { return master_volume_; }

    void set_master_volume(std::uint16_t volume);

    [[nodiscard]] bool sound_on() const { return sound_on_; }

    // SciMusic::soundSetSoundOn: also the device's play switch.
    void set_sound_on(bool on);

    // The device the songs play on; null is the silent driver. It must
    // outlive the player or be detached first. Attached before the sounds
    // are initialised, as in ScummVM (the play mask is applied when a
    // sound is initialised).
    void set_device(audio::MidiDevice* device) { device_ = device; }

    [[nodiscard]] audio::MidiDevice* device() const { return device_; }

    // From the parsers: a MIDI message for the device. Outside the music
    // timer (from kDoSound, ScummVM's main thread) it waits in a queue for
    // the next timer call, as ScummVM's putMidiCommandInQueue does.
    void send(std::uint32_t midi);
    // The sound's header for the device (MidiParser_SCI::initTrack), queued
    // the same way.
    void init_track(MusicEntry& sound);
    void remove_track_inits(const MusicEntry& sound);
    // MidiPlayer::setVolume, never queued.
    void set_device_volume(std::uint8_t volume);

    // What the device (or the silent driver) asks of the player.
    [[nodiscard]] audio::PlayerChoices player_choices() const {
        return device_ != nullptr ? device_->player_choices() : silent_choices;
    }

    // kDoSound GetPolyphony.
    [[nodiscard]] int voices() const { return player_choices().polyphony; }

    // From the parsers: a song's SysEx message (without 0xF0 and 0xF7), at
    // once, as MidiParser::processEvent sends it.
    void sysex(std::span<const std::uint8_t> message) const {
        if (device_ != nullptr) {
            device_->sysex(message);
        }
    }

    [[nodiscard]] std::uint32_t tempo() const { return tempo_; }

    // Adds a new entry to the playlist (pushBackSlot) and returns it.
    MusicEntry& add(std::unique_ptr<MusicEntry> sound);
    [[nodiscard]] MusicEntry* slot(Reg object);
    [[nodiscard]] MusicEntry* first_slot_with_status(SoundStatus status);

    [[nodiscard]] std::size_t size() const { return play_list_.size(); }

    [[nodiscard]] MusicEntry& at(std::size_t index) { return *play_list_[index]; }

    [[nodiscard]] const MusicEntry& at(std::size_t index) const { return *play_list_[index]; }

    [[nodiscard]] std::int8_t global_reverb() const { return global_reverb_; }

    // MidiPlayer::setReverb.
    void set_driver_reverb(std::int8_t reverb) {
        driver_reverb_ = reverb;
        if (device_ != nullptr) {
            device_->set_reverb(reverb);
        }
    }

    // Writes its part of the VM state (state.cpp).
    void save_state(StateWriter& out) const;
    // Reads it back (state_load.cpp).
    void load_state(StateReader& in, Resources& resources);

private:
    // A queued device command: a track init if `track_init` is set, else a
    // MIDI message.
    struct QueuedCommand {
        MusicEntry* track_init = nullptr;
        std::uint32_t midi = 0;
    };

    void send_queued_commands();
    void sort_play_list();
    void sort(std::size_t first, std::size_t last);

    Faults* faults_;
    std::vector<std::unique_ptr<MusicEntry>> play_list_;
    std::uint32_t tempo_ = adlib_base_tempo;
    bool sound_on_ = true;
    std::uint16_t master_volume_ = music_master_volume_max;
    std::int8_t driver_reverb_ = -1;
    std::int8_t global_reverb_;
    int global_pause_ = 0;
    bool needs_resume_ = false;
    int time_counter_ = 0;
    std::uint32_t timer_calls_ = 0;
    audio::MidiDevice* device_ = nullptr;
    std::vector<QueuedCommand> queued_commands_;
    bool in_timer_ = false;
};

} // namespace sci::vm
