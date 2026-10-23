// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   audio/midiparser.cpp (MidiParser)
//   engines/sci/sound/midiparser_sci.cpp (MidiParser_SCI, SCI0 paths)
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

#include "midi_parser.hpp"

#include "faults.hpp"
#include "music.hpp"

#include <array>
#include <format>
#include <utility>

namespace sci::vm {

namespace {

constexpr std::array<int, 8> midi_parameter_count{2, 2, 2, 2, 1, 1, 2, 0};

// The SCI MIDI commands.
constexpr std::uint8_t midi_set_signal_loop = 0x7F;
constexpr std::uint8_t midi_end_of_track = 0xFC;
constexpr std::uint8_t midi_set_reverb = 0x50;
constexpr std::uint8_t midi_hold = 0x52;
constexpr std::uint8_t midi_update_cue = 0x60;
constexpr std::uint8_t midi_reset_on_pause = 0x4C;

int parameter_count(std::uint8_t command) {
    return midi_parameter_count[static_cast<std::size_t>((command >> 4U) - 8U)];
}

void write_delta(std::vector<std::uint8_t>& out, int& delta) {
    while (delta > 240) {
        out.push_back(0xF8);
        delta -= 240;
    }
    out.push_back(static_cast<std::uint8_t>(delta));
}

} // namespace

void MidiParser::load_music(std::span<const std::uint8_t> song, MusicEntry& sound, unsigned channel_mask) {
    unload_music();
    sound_ = &sound;
    channel_used_.fill(false);
    // SCI0 has one data stream; the channels of other devices are filtered
    // out. The mask always holds the control channel, so it is never 0.
    filter_channels(song, channel_mask);
    loaded_ = true;
    set_track();
    loop_tick_ = 0;
}

void MidiParser::unload_music() {
    if (sound_ != nullptr) {
        position_ = Tracker{};
        all_notes_off();
        // Track inits still queued for the device would otherwise run.
        music_->remove_track_inits(*sound_);
    }
    loaded_ = false;
    sound_ = nullptr;
    reset_on_pause_ = false;
    data_.clear();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to MidiParser_SCI::midiFilterChannels.
void MidiParser::filter_channels(std::span<const std::uint8_t> song, unsigned channel_mask) {
    std::size_t in = 0;
    std::uint8_t current_channel = 15;
    std::uint8_t command = 0;
    std::uint8_t last_command = 0;
    int delta = 0;
    int parameters = 0;
    bool contains_midi = false;
    data_.clear();
    data_.reserve(song.size() + 5);
    const auto available = [&](std::size_t count) {
        if (song.size() - in < count) {
            warn("Unexpected end of the sound data. Music may sound wrong due to game resource corruption");
            return false;
        }
        return true;
    };

    while (in < song.size()) {
        const std::uint8_t current_delta = song[in++];
        if (current_delta == midi_end_of_track) {
            break; // It can appear without a delta.
        }
        if (current_delta == 0xF8) {
            delta += 240;
            continue;
        }
        delta += current_delta;
        if (!available(1)) {
            break;
        }
        std::uint8_t current = song[in++];
        if (current == 0xF0 || current == midi_end_of_track) {
            command = current;
            current_channel = 15;
        } else if ((current & 0x80U) != 0) {
            command = current;
            current_channel = command & 0x0FU;
            parameters = parameter_count(command);
        }
        if (((1U << current_channel) & channel_mask) == 0) {
            int count = parameters - 1;
            if ((current & 0x80U) != 0) {
                ++count;
            }
            // A count of -1 fails the check, as the unsigned size it is in
            // ScummVM.
            if (count < 0) {
                warn("Unexpected end of the sound data. Music may sound wrong due to game resource corruption");
                break;
            }
            if (!available(static_cast<std::size_t>(count))) {
                break;
            }
            in += static_cast<std::size_t>(count);
            continue;
        }
        if (current_channel != 0xF) {
            contains_midi = true;
        }
        // Stop at the first end of track; what follows may be junk.
        if (command == midi_end_of_track) {
            break;
        }
        write_delta(data_, delta);
        delta = 0;
        if (command == 0xF0) {
            data_.push_back(command);
            bool ended = false;
            do {
                if (!available(1)) {
                    ended = true;
                    break;
                }
                current = song[in++];
                data_.push_back(current);
            } while (current != 0xF7);
            if (ended) {
                break;
            }
            last_command = command;
            continue;
        }
        // The channels used, for the init commands.
        channel_used_[command & 0x0FU] = true;
        if (last_command != command) {
            data_.push_back(command);
            last_command = command;
        }
        if (parameters > 0) {
            if ((current & 0x80U) != 0) {
                if (!available(1)) {
                    break;
                }
                data_.push_back(song[in++]);
            } else {
                data_.push_back(current);
            }
        }
        if (parameters > 1) {
            if (!available(1)) {
                break;
            }
            data_.push_back(song[in++]);
        }
    }
    // The stop event.
    write_delta(data_, delta);
    data_.insert(data_.end(), {0xFF, 0x2F, 0x00, 0x00});
    if (!contains_midi) {
        warn("MIDI parser: the requested SCI0 sound has no MIDI note data for the currently selected sound driver");
    }
}

void MidiParser::set_track() {
    position_ = Tracker{};
    next_event_.clear();
    position_.play_pos = 0;
    parse_next_event(next_event_);
}

std::uint32_t MidiParser::read_vlq(std::size_t& position) const {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        const std::uint8_t part = byte_at(position++);
        value = (value << 7U) | (part & 0x7FU);
        if ((part & 0x80U) == 0) {
            break;
        }
    }
    return value;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to MidiParser_SCI::parseNextEvent.
void MidiParser::parse_next_event(EventInfo& info) {
    std::size_t position = position_.play_pos.value_or(0); // Only called while tracking.
    info.start = position;
    info.delta = 0;
    while (byte_at(position) == 0xF8) {
        info.delta += 240;
        ++position;
    }
    info.delta += byte_at(position++);

    if ((byte_at(position) & 0xF0U) >= 0x80) {
        info.event = byte_at(position++);
    } else {
        info.event = position_.running_status;
    }
    if (info.event < 0x80) {
        position_.play_pos = position;
        return;
    }
    position_.running_status = info.event;
    switch (info.command()) {
    case 0xC:
    case 0xD:
        info.param1 = byte_at(position++);
        info.param2 = 0;
        break;
    case 0xB:
        info.param1 = byte_at(position++);
        info.param2 = byte_at(position++);
        info.length = 0;
        break;
    case 0x8:
    case 0x9:
    case 0xA:
    case 0xE:
        info.param1 = byte_at(position++);
        info.param2 = byte_at(position++);
        if (info.command() == 0x9 && info.param2 == 0) {
            info.event = info.channel() | 0x80U; // A note on with velocity 0 is a note off.
        }
        info.length = 0;
        break;
    case 0xF:
        switch (info.event & 0x0FU) {
        case 0x2:
            info.param1 = byte_at(position++);
            info.param2 = byte_at(position++);
            break;
        case 0x3:
            info.param1 = byte_at(position++);
            info.param2 = 0;
            break;
        case 0x6:
        case 0x8:
        case 0xA:
        case 0xB:
        case 0xC:
        case 0xE:
            info.param1 = 0;
            info.param2 = 0;
            break;
        case 0x0: // SysEx.
            info.length = read_vlq(position);
            info.data = position;
            position += info.length;
            break;
        case 0xF: // Meta event.
            info.param1 = byte_at(position++);
            info.length = read_vlq(position);
            info.data = position;
            position += info.length;
            break;
        default:
            warn(std::format("MidiParser_SCI::parseNextEvent: Unsupported event code {:x}", info.event));
            break;
        }
        break;
    default:
        break;
    }
    position_.play_pos = position;
}

void MidiParser::advance_position(std::uint32_t event_tick) {
    const std::uint32_t event_time =
        position_.last_event_time + ((event_tick - position_.last_event_tick) * psec_per_tick_);
    position_.play_time = event_time;
    position_.last_event_time = event_time;
    position_.sub_last_event_time = event_time;
    position_.play_tick = event_tick;
    position_.last_event_tick = event_tick;
    position_.sub_last_event_tick = event_tick;
    if (position_.play_pos) {
        parse_next_event(next_event_);
    }
}

void MidiParser::on_timer() {
    if (!position_.play_pos) {
        return;
    }
    abort_parse_ = false;
    const std::uint32_t end_time = position_.play_time + timer_rate_;
    while (!abort_parse_) {
        const EventInfo& info = next_event_;
        const std::uint32_t event_tick = position_.sub_last_event_tick + info.delta;
        const std::uint32_t event_time =
            position_.last_event_time + ((event_tick - position_.last_event_tick) * psec_per_tick_);
        if (event_time > end_time) {
            break;
        }
        if (info.event < 0x80) {
            warn(std::format("Bad command or running status {:02X}", info.event));
            position_.play_pos.reset();
            return;
        }
        // The event is copied: processing may parse the next one into
        // next_event_ (a jump does).
        const EventInfo event = info;
        if (event.command() == 0x8) {
            active_note(event.channel(), event.param1, false);
        } else if (event.command() == 0x9) {
            active_note(event.channel(), event.param1, true);
        }
        if (!process_event(event)) {
            return;
        }
        if (!abort_parse_) {
            advance_position(event_tick);
        }
    }
    if (!abort_parse_) {
        position_.play_time = end_time;
        position_.play_tick = ((end_time - position_.last_event_time) / psec_per_tick_) + position_.last_event_tick;
    }
}

// Kept close to MidiParser_SCI::processEvent. A hold or loop jumps back,
// which replays events; jumps do not nest.
// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion)
bool MidiParser::process_event(const EventInfo& info, bool fire_events) {
    if (!fire_events) {
        return process_base_event(info, fire_events);
    }
    switch (info.command()) {
    case 0xC:
        if (info.channel() == 0xF) {
            if (info.param1 == midi_set_signal_loop) {
                // Not passed on to scripts after SCI0 early.
                loop_tick_ = position_.play_tick;
                return true;
            }
            // SCI0 signals are never skipped, not even on tick 0.
            if (!jumping_to_tick_) {
                sound_->set_signal(info.param1);
            }
            return true;
        }
        break;
    case 0xB:
        if (info.param1 == midi_set_reverb) {
            sound_->reverb = info.param2 == 127 ? music_->global_reverb() : static_cast<std::int8_t>(info.param2);
            music_->set_driver_reverb(sound_->reverb);
        }
        if (info.channel() == 0xF) {
            switch (info.param1) {
            case midi_set_reverb:
                return true;
            case midi_hold:
                // Loop back without stopping notes when the hold marker is
                // the one cmdSetSoundHold set.
                if (std::cmp_equal(info.param2, sound_->hold)) {
                    jump_to_tick(loop_tick_, false, false);
                }
                return true;
            case midi_update_cue:
                if (!jumping_to_tick_) {
                    sound_->data_inc = static_cast<std::uint16_t>(sound_->data_inc + info.param2);
                }
                return true;
            case midi_reset_on_pause:
                reset_on_pause_ = info.param2 != 0;
                return true;
            case 0x46:
            case 0x61:
            case 0x73:
            case 0xD1:
                return true; // Obscure commands, ignored.
            case 0x01:
            case 0x04:
            case 0x07:
            case 0x0A:
            case 0x0B:
            case 0x40:
            case 0x79:
            case 0x7B:
                break; // Handled by the driver.
            case 0x4B:
                warn(std::format("Unhanded SCI MIDI command 0x{:x} - voice mapping (parameter {})", info.param1,
                                 info.param2));
                return true;
            default:
                warn(std::format("Unhandled SCI MIDI command 0x{:x} (parameter {})", info.param1, info.param2));
                return true;
            }
        }
        break;
    case 0xF:
        if (info.param1 == 0x2F) { // End of track.
            if (sound_->loop != 0) {
                --sound_->loop;
            }
            if (sound_->loop != 0 || sound_->hold > 0) {
                jump_to_tick(loop_tick_);
                return true;
            }
            sound_->set_signal(signal_offset);
        }
        break;
    default:
        break;
    }
    return process_base_event(info, fire_events);
}

bool MidiParser::process_base_event(const EventInfo& info, bool fire_events) {
    // MidiParser::processEvent. SysEx is dropped: the AdLib driver ignores
    // it and takes no time for it, so no SysEx delay arises.
    if (info.event == 0xF0) {
        return true;
    }
    if (info.event == 0xFF) {
        if (info.param1 == 0x2F) {
            position_.play_pos.reset();
            stop_playing();
            return false;
        }
        if (info.param1 == 0x51 && info.length >= 3) {
            set_tempo((static_cast<std::uint32_t>(byte_at(info.data)) << 16U) |
                      (static_cast<std::uint32_t>(byte_at(info.data + 1)) << 8U) | byte_at(info.data + 2));
        }
        return true; // Meta events are not sent to the SCI drivers.
    }
    if (fire_events) {
        send_to_driver(info.event, info.param1, info.param2);
    }
    return true;
}

void MidiParser::send_to_driver(std::uint32_t midi) {
    if ((midi & 0xFFF0U) == 0x07B0U) {
        // Channel volume, scaled by the song's volume.
        const std::uint32_t volume = ((midi >> 16U) & 0xFFU) * volume_ / 127U;
        midi = (midi & 0xFFFFU) | ((volume & 0xFFU) << 16U);
    }
    music_->send(midi);
}

void MidiParser::active_note(std::uint8_t channel, std::uint8_t note, bool active) {
    if (note >= 128 || channel >= 16) {
        return;
    }
    const unsigned bit = 1U << channel;
    if (active) {
        active_notes_[note] = static_cast<std::uint16_t>(active_notes_[note] | bit);
    } else {
        active_notes_[note] = static_cast<std::uint16_t>(active_notes_[note] & ~bit);
    }
}

void MidiParser::all_notes_off() {
    // MidiParser_SCI::allNotesOff: every channel is mapped in SCI0.
    for (std::size_t note = 0; note < active_notes_.size(); ++note) {
        for (unsigned channel = 0; channel < 16; ++channel) {
            if ((active_notes_[note] & (1U << channel)) != 0) {
                send_to_driver(static_cast<std::uint8_t>(0x80U | channel), static_cast<std::uint8_t>(note), 0);
            }
        }
    }
    for (unsigned channel = 0; channel < 16; ++channel) {
        send_to_driver(static_cast<std::uint8_t>(0xB0U | channel), 0x7B, 0); // All notes off.
        send_to_driver(static_cast<std::uint8_t>(0xB0U | channel), 0x40, 0); // Sustain off (ScummVM bug #5524).
    }
    active_notes_.fill(0);
}

void MidiParser::send_init_commands() {
    volume_ = 127;
    for (std::uint8_t channel = 0; channel < 16; ++channel) {
        if (channel_used_[channel]) {
            send_to_driver(static_cast<std::uint8_t>(0xB0U | channel), 0x07, 127); // Volume to the maximum.
            send_to_driver(static_cast<std::uint8_t>(0xB0U | channel), 0x0A, 64);  // Panning to the centre.
            send_to_driver(static_cast<std::uint8_t>(0xB0U | channel), 0x40, 0);   // Hold pedal off.
            send_to_driver(static_cast<std::uint8_t>(0xE0U | channel), 0, 64);     // Pitch wheel to the centre.
        }
    }
}

void MidiParser::init_track() {
    if (sound_ != nullptr && loaded_) {
        music_->init_track(*sound_);
    }
}

void MidiParser::stop_playing() {
    position_ = Tracker{};
}

void MidiParser::pause() {
    all_notes_off();
    if (reset_on_pause_) {
        jump_to_tick(0);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion): kept close to MidiParser::jumpToTick.
bool MidiParser::jump_to_tick(std::uint32_t tick, bool fire_events, bool stop_notes, bool dont_send_note_on) {
    if (!loaded_) {
        return false;
    }
    if (jumping_to_tick_) {
        // An assertion in ScummVM: jumpToTick is not re-entrant.
        warn("MidiParser::jumpToTick is not re-entrant");
        return false;
    }
    jumping_to_tick_ = true;
    const Tracker current_position = position_;
    const EventInfo current_event = next_event_;

    position_ = Tracker{};
    position_.play_pos = 0;
    parse_next_event(next_event_);
    if (tick > 0) {
        while (true) {
            const EventInfo info = next_event_;
            const std::uint32_t event_tick = position_.sub_last_event_tick + info.delta;
            if (event_tick >= tick) {
                position_.play_time += (tick - position_.last_event_tick) * psec_per_tick_;
                position_.play_tick = tick;
                break;
            }
            if (info.command() == 0x9 && dont_send_note_on) {
                // Notes are not started when fast-forwarding.
            } else if (info.event == 0xFF && info.param1 == 0x2F) {
                position_ = current_position;
                next_event_ = current_event;
                jumping_to_tick_ = false;
                return false;
            } else {
                process_event(info, fire_events);
            }
            advance_position(event_tick);
        }
    }
    if (stop_notes) {
        all_notes_off();
    }
    abort_parse_ = true;
    jumping_to_tick_ = false;
    return true;
}

void MidiParser::set_master_volume(std::uint8_t volume) {
    master_volume_ = volume;
    set_volume(volume_);
}

void MidiParser::set_volume(std::uint8_t volume) {
    // The SCI0 AdLib driver has one global volume, set directly (not
    // queued) from either thread.
    volume_ = volume;
    music_->set_device_volume(static_cast<std::uint8_t>(volume_ * master_volume_ / music_volume_max));
}

} // namespace sci::vm
