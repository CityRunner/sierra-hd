// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/sound/soundcmd.cpp (SoundCommandParser, SCI0 paths)
//   engines/sci/sci.cpp (SciEngine::updateSoundMixerVolumes)
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

#include "sound.hpp"

#include "machine.hpp"
#include "music.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace sci::vm {

namespace {

// ScummVM's default music volume (192 of 256) as master volume:
// (192 + 1) * 15 / 256.
constexpr std::uint16_t default_master_volume = (192 + 1) * music_master_volume_max / 256;

std::uint16_t read_value(Machine& machine, Reg object, Selector selector) {
    return machine.read_selector(object, selector).to_uint16();
}

void write_value(Machine& machine, Reg object, Selector selector, int value) {
    machine.write_selector_value(object, selector, value);
}

std::string reg_text(Reg value) {
    return std::format("{:04x}:{:04x}", value.segment, value.offset);
}

} // namespace

Sound::Sound() = default;

Sound::~Sound() = default;

void Sound::attach(Machine& machine) {
    music_ = std::make_unique<Music>(machine.faults());
    music_->set_device(device_);
    // SciMusic::init: in the deterministic mode the virtual clock runs the
    // music timer once per base tempo of the driver.
    machine.subsystems().clock().set_music_timer([this] { music_->on_timer(); }, music_->tempo());
    // SciEngine::syncSoundSettings.
    music_->set_master_volume(default_master_volume);
}

void Sound::clear_play_list() {
    if (music_) {
        music_->clear_play_list();
    }
}

void Sound::update_sci0_cues(Machine& machine) {
    // The playlist may be re-sorted while cues are processed; ScummVM walks
    // it with an iterator, which an index into the same array matches.
    for (std::size_t i = 0; i < music_->size(); ++i) {
        const MusicEntry& sound = std::as_const(*music_).at(i);
        if (sound.status == SoundStatus::Playing || sound.signal != 0) {
            process_update_cues(machine, sound.sound_object);
        }
    }
}

Reg Sound::init(Machine& machine, std::span<Reg> args) {
    process_init_sound(machine, args[0]);
    return machine.acc();
}

void Sound::process_init_sound(Machine& machine, Reg object) {
    const SelectorCache& selector = machine.selector();
    const std::uint16_t resource_id = read_value(machine, object, selector.number);
    if (MusicEntry* old = music_->slot(object)) {
        music_->sound_kill(*old);
    }
    auto entry = std::make_unique<MusicEntry>();
    entry->resource_id = resource_id;
    entry->sound_object = object;
    entry->loop = read_value(machine, object, selector.loop);
    entry->override_priority = false;
    entry->priority = static_cast<std::int16_t>(read_value(machine, object, selector.priority));
    entry->reverb = -1;
    // initSoundResource.
    if (resource_id != 0) {
        if (const auto* data = machine.resources().find(ResourceType::Sound, resource_id)) {
            entry->resource = SoundResource::parse(*data, resource_id);
            if (!entry->resource) {
                machine.faults().raise(std::format("sound.{:03} is too short", resource_id));
                return;
            }
        }
    }
    if (entry->resource) {
        music_->sound_init(*entry);
    }
    const MusicEntry& sound = music_->add(std::move(entry));
    if (sound.resource) {
        write_value(machine, object, selector.state, static_cast<int>(SoundStatus::Initialized));
    }
}

Reg Sound::play(Machine& machine, std::span<Reg> args) {
    process_play_sound(machine, args[0]);
    return machine.acc();
}

void Sound::reconstruct_play_list(Machine& machine) {
    if (!music_) {
        return;
    }
    // Starting songs may re-sort the list: the songs as they are now.
    std::vector<Reg> songs;
    songs.reserve(music_->size());
    for (std::size_t i = 0; i < music_->size(); ++i) {
        songs.push_back(std::as_const(*music_).at(i).sound_object);
    }
    const SelectorCache& selector = machine.selector();
    const auto replay = [&](Reg object) {
        const MusicEntry* entry = music_->slot(object);
        if (entry == nullptr) {
            return;
        }
        // The object's loop and priority are those of the song (bug #5501).
        write_value(machine, object, selector.loop, entry->loop);
        write_value(machine, object, selector.priority, entry->priority);
        process_play_sound(machine, object, true);
    };
    for (const Reg object : songs) {
        MusicEntry* entry = music_->slot(object);
        if (entry == nullptr) {
            continue;
        }
        // initSoundResource: ScummVM gives every song with a resource a new
        // parser (soundInitSnd); the state keeps the parser, which takes the
        // new one's volumes, and the device gets the volume as in ScummVM.
        if (entry->parser) {
            entry->parser->reset_volumes(static_cast<std::uint8_t>(music_->master_volume()));
        }
        if (entry->status == SoundStatus::Playing) {
            replay(object);
        }
    }
    if (music_->first_slot_with_status(SoundStatus::Playing) == nullptr) {
        if (const MusicEntry* paused = music_->first_slot_with_status(SoundStatus::Paused)) {
            replay(paused->sound_object);
        }
    }
}

void Sound::process_play_sound(Machine& machine, Reg object, bool restoring) {
    const SelectorCache& selector = machine.selector();
    MusicEntry* slot = music_->slot(object);
    if (slot == nullptr) {
        warn(std::format("kDoSound(play): Slot not found ({}), initializing it manually", reg_text(object)));
        process_init_sound(machine, object);
        slot = music_->slot(object);
        if (slot == nullptr) {
            machine.faults().raise("Failed to initialize uninitialized sound slot");
            return;
        }
    }
    // A restored song plays the resource it was playing, even if the object
    // names the next one already (bug #10907).
    const std::uint16_t resource_id = restoring ? slot->resource_id : read_value(machine, object, selector.number);
    if (slot->resource_id != resource_id) {
        // Another sound was loaded into the object.
        process_dispose_sound(machine, object);
        process_init_sound(machine, object);
        slot = music_->slot(object);
        if (slot == nullptr) {
            return;
        }
    }
    machine.write_selector(object, selector.handle, object);
    write_value(machine, object, selector.state, static_cast<int>(SoundStatus::Playing));
    slot->loop = read_value(machine, object, selector.loop);
    // SCI0 resources carry no priority: the object's is used.
    slot->priority = static_cast<std::int16_t>(read_value(machine, object, selector.priority));
    // Hold is reset when a song starts; kDoSoundSetHold follows if needed.
    slot->hold = -1;
    if (!restoring) {
        slot->play_bed = false; // SCI0's play has no bed argument; a restore keeps the song's.
    }
    music_->sound_play(*slot, restoring);
    // Left-over signals are reset.
    slot->signal = 0;
    slot->fade_step = 0;
}

Reg Sound::dispose(Machine& machine, std::span<Reg> args) {
    process_dispose_sound(machine, args[0]);
    return machine.acc();
}

void Sound::process_dispose_sound(Machine& machine, Reg object) {
    MusicEntry* slot = music_->slot(object);
    if (slot == nullptr) {
        warn(std::format("kDoSound(dispose): Slot not found ({})", reg_text(object)));
        return;
    }
    process_stop_sound(machine, object, false);
    music_->sound_kill(*slot);
    write_value(machine, object, machine.selector().handle, 0);
    write_value(machine, object, machine.selector().state, static_cast<int>(SoundStatus::Stopped));
}

Reg Sound::stop(Machine& machine, std::span<Reg> args) {
    process_stop_sound(machine, args[0], false);
    return machine.acc();
}

void Sound::process_stop_sound(Machine& machine, Reg object, bool sample_finished_playing) {
    const SelectorCache& selector = machine.selector();
    MusicEntry* slot = music_->slot(object);
    if (slot == nullptr) {
        warn(std::format("kDoSound(stop): Slot not found ({})", reg_text(object)));
        return;
    }
    write_value(machine, object, selector.state, static_cast<int>(SoundStatus::Stopped));
    // SCI0 sets the signal selector only when a sample has finished.
    if (sample_finished_playing) {
        write_value(machine, object, selector.signal, signal_offset);
    }
    slot->data_inc = 0;
    slot->signal = signal_offset;
    music_->sound_stop(*slot);
    if (const MusicEntry* playing = music_->first_slot_with_status(SoundStatus::Playing)) {
        write_value(machine, playing->sound_object, selector.state, static_cast<int>(SoundStatus::Playing));
    }
}

Reg Sound::pause(Machine& machine, std::span<Reg> args) {
    // SCI0 pauses (1) or resumes (0) the current music.
    const SelectorCache& selector = machine.selector();
    MusicEntry* slot = music_->first_slot_with_status(SoundStatus::Playing);
    switch (args[0].to_uint16()) {
    case 1:
        if (slot != nullptr) {
            music_->sound_pause(*slot);
            write_value(machine, slot->sound_object, selector.state, static_cast<int>(SoundStatus::Paused));
        }
        return number(0);
    case 0:
        if (slot == nullptr) {
            slot = music_->first_slot_with_status(SoundStatus::Paused);
            if (slot != nullptr) {
                music_->sound_resume(*slot);
                write_value(machine, slot->sound_object, selector.state, static_cast<int>(SoundStatus::Playing));
                return number(1);
            }
        }
        return number(0);
    default:
        machine.faults().raise("kDoSound(pause): parameter 0 is invalid for sound-sci0");
        return machine.acc();
    }
}

Reg Sound::mute(Machine& /*machine*/, std::span<Reg> args) {
    const bool previous = music_->sound_on();
    if (!args.empty()) {
        music_->set_sound_on(args[0].to_uint16() != 0);
    }
    return number(previous ? 1 : 0);
}

Reg Sound::master_volume(Machine& /*machine*/, std::span<Reg> args) {
    const Reg previous = number(music_->master_volume());
    if (!args.empty()) {
        // Through the menu bar ScummVM routes the volume via its own
        // setting first; the volume that comes back is the same.
        const int volume = std::clamp(static_cast<int>(args[0].to_sint16()), 0, music_master_volume_max);
        music_->set_master_volume(static_cast<std::uint16_t>(volume));
    }
    return previous;
}

Reg Sound::update(Machine& machine, std::span<Reg> args) {
    const SelectorCache& selector = machine.selector();
    const Reg object = args[0];
    MusicEntry* slot = music_->slot(object);
    if (slot == nullptr) {
        warn(std::format("kDoSound(update): Slot not found ({})", reg_text(object)));
        return machine.acc();
    }
    slot->loop = read_value(machine, object, selector.loop);
    // SCI0 objects have no vol selector, which reads as 0.
    const int object_volume = std::clamp(static_cast<int>(read_value(machine, object, selector.vol)), 0, 255);
    if (object_volume != slot->volume) {
        music_->sound_set_volume(*slot, object_volume);
    }
    const auto object_priority = static_cast<std::int16_t>(read_value(machine, object, selector.priority));
    if (object_priority != slot->priority) {
        music_->sound_set_priority(*slot, static_cast<std::uint8_t>(object_priority));
    }
    return machine.acc();
}

Reg Sound::fade(Machine& machine, std::span<Reg> args) {
    const Reg object = args[0];
    // Null in several SCI0 games; ignored.
    if (object.is_null() && args.size() == 1) {
        return machine.acc();
    }
    MusicEntry* slot = music_->slot(object);
    if (slot == nullptr) {
        return machine.acc();
    }
    if (args.size() != 1) {
        machine.faults().raise(std::format("kDoSound(fade): unsupported argc {}", args.size()));
        return machine.acc();
    }
    if (slot->status != SoundStatus::Playing) {
        write_value(machine, object, machine.selector().signal, signal_offset);
        return machine.acc();
    }
    // SCI0 fades out and then stops the music.
    slot->fade_to = 0;
    slot->fade_step = -5;
    slot->fade_ticker_step = 10 * sci_tick_microseconds / music_->tempo();
    slot->fade_ticker = 0;
    return machine.acc();
}

Reg Sound::polyphony(Machine& /*machine*/, std::span<Reg> /*args*/) {
    return number(music_->voices());
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): kept close to SoundCommandParser::processUpdateCues.
void Sound::process_update_cues(Machine& machine, Reg object) {
    const SelectorCache& selector = machine.selector();
    MusicEntry* slot = music_->slot(object);
    if (slot == nullptr) {
        warn(std::format("kDoSound(updateCues): Slot not found ({})", reg_text(object)));
        return;
    }
    if (slot->parser) {
        if (slot->signal == 0) {
            if (slot->data_inc != read_value(machine, object, selector.data_inc)) {
                if (selector.data_inc != no_selector) {
                    write_value(machine, object, selector.data_inc, slot->data_inc);
                }
                write_value(machine, object, selector.signal, slot->data_inc + 127);
            }
        } else {
            write_value(machine, object, selector.signal, slot->signal);
            // The state selector needs updating too.
            if (slot->signal == signal_offset) {
                process_stop_sound(machine, object, false);
            }
        }
    } else {
        // No data for the selected device: the sound stops, which also sets
        // the signal selector.
        process_stop_sound(machine, object, true);
    }
    if (slot->fade_completed) {
        slot->fade_completed = false;
        write_value(machine, object, selector.signal, signal_offset);
        process_stop_sound(machine, object, false);
    }
    write_value(machine, object, selector.loop, slot->loop);
    slot->signal = 0;
}

} // namespace sci::vm
