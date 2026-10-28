// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 MIDI parser: plays the song data of a sound resource on the
// music timer and raises the signals and cues that scripts wait for, and
// sends its MIDI messages to the player's device (Music::send), which drops
// them when there is none (the silent driver). Ported from
// ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   audio/midiparser.{h,cpp} (MidiParser, one track with one subtrack),
//   engines/sci/sound/midiparser_sci.{h,cpp} (MidiParser_SCI, SCI0 paths)
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sci::vm {

class StateWriter;
class Music;
struct MusicEntry;

// The MIDI timing of SCI songs: one tick is 1/60 s.
inline constexpr std::uint32_t sci_tick_microseconds = 16667;

class MidiParser {
public:
    explicit MidiParser(Music& music) : music_(&music) {}

    // Loads the SCI0 song data (after the 0x21-byte header), keeping the
    // channels in `channel_mask`, and starts at its beginning.
    void load_music(std::span<const std::uint8_t> song, MusicEntry& sound, unsigned channel_mask);
    void unload_music();

    void set_timer_rate(std::uint32_t microseconds) { timer_rate_ = microseconds; }

    void set_master_volume(std::uint8_t volume);
    void set_volume(std::uint8_t volume);

    // MidiParser_SCI::sendInitCommands: resets the channels the song uses.
    void send_init_commands();
    // MidiParser_SCI::initTrack: the header goes to the device.
    void init_track();

    void stop() {
        abort_parse_ = true;
        all_notes_off();
    }

    void pause();

    // Called once per timer period: plays the events that are due.
    void on_timer();
    bool jump_to_tick(std::uint32_t tick, bool fire_events = false, bool stop_notes = true,
                      bool dont_send_note_on = false);

    [[nodiscard]] std::uint32_t tick() const { return position_.play_tick; }

    [[nodiscard]] bool is_tracking() const { return position_.play_pos.has_value(); }

    // Writes its part of the VM state (state.cpp).
    void save_state(StateWriter& out) const;

private:
    struct EventInfo {
        std::size_t start = 0;
        std::uint32_t delta = 0;
        std::uint8_t event = 0;
        // basic.param1 and ext.type share their storage in ScummVM.
        std::uint8_t param1 = 0;
        std::uint8_t param2 = 0;
        std::size_t data = 0; // ext.data, as an index into the song.
        std::uint32_t length = 0;

        [[nodiscard]] std::uint8_t channel() const { return event & 0x0FU; }

        [[nodiscard]] std::uint8_t command() const { return event >> 4U; }

        void clear() { *this = EventInfo{}; }
    };

    struct Tracker {
        std::uint32_t play_time = 0;
        std::uint32_t play_tick = 0;
        std::uint32_t last_event_time = 0;
        std::uint32_t last_event_tick = 0;
        // The one subtrack: the next event to parse, if still tracking.
        std::optional<std::size_t> play_pos;
        std::uint32_t sub_last_event_time = 0;
        std::uint32_t sub_last_event_tick = 0;
        std::uint8_t running_status = 0;
    };

    [[nodiscard]] std::uint8_t byte_at(std::size_t index) const { return index < data_.size() ? data_[index] : 0; }

    std::uint32_t read_vlq(std::size_t& position) const;
    void filter_channels(std::span<const std::uint8_t> song, unsigned channel_mask);
    void set_track();
    void parse_next_event(EventInfo& info);
    bool process_event(const EventInfo& info, bool fire_events = true);
    bool process_base_event(const EventInfo& info, bool fire_events);
    void stop_playing();
    // MidiParser_SCI::sendToDriver, SCI0: the channel volume follows the
    // song's volume; channels are not remapped.
    void send_to_driver(std::uint32_t midi);

    void send_to_driver(std::uint8_t status, std::uint8_t first, std::uint8_t second) {
        send_to_driver(status | (static_cast<std::uint32_t>(first) << 8U) |
                       (static_cast<std::uint32_t>(second) << 16U));
    }

    // MidiParser::activeNote, without hanging notes: SCI notes have no
    // length and the parser makes no smart jumps.
    void active_note(std::uint8_t channel, std::uint8_t note, bool active);
    void all_notes_off();

    void set_tempo(std::uint32_t tempo) { psec_per_tick_ = tempo; } // _ppqn is 1.

    void advance_position(std::uint32_t event_tick);

    Music* music_;
    MusicEntry* sound_ = nullptr;
    std::vector<std::uint8_t> data_; // The filtered song.
    bool loaded_ = false;            // _numTracks == 1 and _activeTrack == 0.
    Tracker position_;
    EventInfo next_event_;
    std::uint32_t timer_rate_ = 0x4A0000;
    std::uint32_t psec_per_tick_ = sci_tick_microseconds;
    bool abort_parse_ = false;
    bool jumping_to_tick_ = false;
    std::uint32_t loop_tick_ = 0;
    std::uint8_t master_volume_ = 15;
    std::uint8_t volume_ = 127;
    bool reset_on_pause_ = false;
    // The channels of each note playing, as bits.
    std::array<std::uint16_t, 128> active_notes_{};
    std::array<bool, 16> channel_used_{};
};

} // namespace sci::vm
