// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/sound/music.cpp (SciMusic, MusicEntry; SCI0 paths)
//   engines/sci/resource/resource_audio.cpp (SoundResource, SCI0 late)
//   common/algorithm.h (Common::sort, whose order of equal elements the
//   playlist keeps)
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

#include "music.hpp"

#include "faults.hpp"
#include "sci/audio/adlib.hpp"
#include "sci/audio/mt32_driver.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace sci::vm {

// The silent driver reports what the AdLib driver does, so the player makes
// the same choices with either (docs/ASSUMPTIONS.md, "Milestone 7: the AdLib
// driver").
static_assert(adlib_play_id == audio::AdlibDriver::play_id);
static_assert(adlib_polyphony == audio::AdlibDriver::polyphony);
static_assert(adlib_has_rhythm_channel == audio::AdlibDriver::has_rhythm_channel);
static_assert(adlib_base_tempo == audio::AdlibDriver::base_tempo);
// The MT-32 runs the music timer at the same rate, so the timing does not
// depend on the device ("Milestone 7: the MT-32 driver").
static_assert(adlib_base_tempo == audio::Mt32Driver::base_tempo);

namespace {

constexpr std::size_t sci0_header_size = 0x21;
constexpr std::uint8_t midi_end_of_track = 0xFC;

// A larger priority value has higher priority; for equal priorities, songs
// added later come first.
bool plays_before(const MusicEntry& left, const MusicEntry& right) {
    return left.priority > right.priority || (left.priority == right.priority && left.time > right.time);
}

} // namespace

// SoundResource -------------------------------------------------------------------

std::optional<SoundResource> SoundResource::parse(std::span<const std::uint8_t> data, int number) {
    if (data.size() < sci0_header_size) {
        return std::nullopt;
    }
    SoundResource resource{.data = data, .song = data.subspan(sci0_header_size), .has_digital_sample = false};
    // A digital sample may follow the end of the MIDI stream.
    if (data[0] == 2) {
        std::size_t end = 0;
        while (end < resource.song.size() && resource.song[end] != midi_end_of_track) {
            ++end;
        }
        while (end < resource.song.size() && resource.song[end] == midi_end_of_track) {
            ++end;
        }
        if (end < resource.song.size()) {
            resource.has_digital_sample = true;
            resource.song = resource.song.first(end);
        } else {
            warn(std::format("No digital sample data in sound resource {}", number));
        }
    }
    return resource;
}

unsigned SoundResource::channel_filter_mask(unsigned hardware_mask, bool wants_rhythm) const {
    // After the digital sample flag, two bytes per channel: the voice count
    // and the play mask of the devices that play the channel.
    unsigned mask = 0;
    for (std::size_t channel = 0; channel < 16; ++channel) {
        mask >>= 1U;
        const std::uint8_t flags = data[2 + (channel * 2)];
        bool play = false;
        if (channel == 15) {
            play = true; // The control channel.
        } else if (channel == 9) {
            play = wants_rhythm || (flags & hardware_mask) != 0;
        } else {
            play = (flags & hardware_mask) != 0;
        }
        if (play) {
            mask |= 0x8000U;
        }
    }
    return mask;
}

// MusicEntry ------------------------------------------------------------------------

void MusicEntry::on_timer() {
    if (signal == 0 && !signal_queue.empty()) {
        signal = signal_queue.front();
        signal_queue.erase(signal_queue.begin());
    }
    if (status != SoundStatus::Playing || loop == 0) {
        return;
    }
    if (fade_step != 0) {
        do_fade();
    }
    if (parser) {
        parser->on_timer();
        ticker = static_cast<std::uint16_t>(parser->tick());
    }
}

void MusicEntry::do_fade() {
    if (fade_ticker != 0) {
        --fade_ticker;
        return;
    }
    fade_ticker = fade_ticker_step;
    volume = static_cast<std::int16_t>(volume + fade_step);
    if ((fade_step > 0 && std::cmp_greater_equal(volume, fade_to)) ||
        (fade_step < 0 && std::cmp_less_equal(volume, fade_to))) {
        volume = fade_to;
        fade_step = 0;
        fade_completed = true;
    }
    if (parser) {
        parser->set_volume(static_cast<std::uint8_t>(volume));
    }
    fade_set_volume = true;
}

void MusicEntry::set_signal(int new_signal) {
    // SCI0 queues signals: songs may signal faster than kGetEvent passes
    // them on to the scripts.
    if (signal == 0) {
        signal = static_cast<std::uint16_t>(new_signal);
    } else {
        signal_queue.push_back(static_cast<std::uint16_t>(new_signal));
    }
}

// Music -------------------------------------------------------------------------------

// SciMusic::init: the driver opens and reports its timer rate; SCI0 takes
// the global reverb from it.
Music::Music(Faults& faults) : faults_(&faults), global_reverb_(driver_reverb_) {
}

void Music::on_timer() {
    // SciMusic::onTimer, then the rest of MidiDriver_AdLib::onTimer.
    ++timer_calls_;
    in_timer_ = true;
    send_queued_commands();
    // The playlist does not change while the timer runs.
    for (const auto& sound : play_list_) {
        sound->on_timer();
    }
    in_timer_ = false;
    if (device_ != nullptr) {
        device_->on_timer();
    }
}

void Music::send(std::uint32_t midi) {
    if (device_ == nullptr) {
        return;
    }
    if (in_timer_) {
        device_->send(midi);
    } else {
        queued_commands_.push_back({.track_init = nullptr, .midi = midi});
    }
}

void Music::init_track(MusicEntry& sound) {
    if (device_ == nullptr || !sound.resource) {
        return;
    }
    if (in_timer_) {
        device_->init_track(sound.resource->data.first(sci0_header_size));
        // MidiPlayer_Midi::initTrack sets the driver's reverb to its default.
        if (const std::int8_t reverb = device_->player_choices().default_reverb; reverb >= 0) {
            driver_reverb_ = reverb;
        }
    } else {
        queued_commands_.push_back({.track_init = &sound, .midi = 0});
    }
}

void Music::remove_track_inits(const MusicEntry& sound) {
    std::erase_if(queued_commands_, [&](const QueuedCommand& command) { return command.track_init == &sound; });
}

void Music::send_queued_commands() {
    // SciMusic::sendMidiCommandsFromQueue: a track init runs only if its
    // sound is still in the playlist with a parser.
    for (const QueuedCommand& command : queued_commands_) {
        if (command.track_init == nullptr) {
            device_->send(command.midi);
            continue;
        }
        const auto found = std::ranges::find_if(
            play_list_, [&](const std::unique_ptr<MusicEntry>& entry) { return entry.get() == command.track_init; });
        if (found != play_list_.end() && (*found)->parser) {
            (*found)->parser->init_track();
        }
    }
    queued_commands_.clear();
}

void Music::set_device_volume(std::uint8_t volume) {
    if (device_ != nullptr) {
        device_->set_volume(volume);
    }
}

void Music::set_sound_on(bool on) {
    sound_on_ = on;
    if (device_ != nullptr) {
        device_->play_switch(on);
    }
}

void Music::clear_play_list() {
    while (!play_list_.empty()) {
        sound_stop(*play_list_.front());
        sound_kill(*play_list_.front());
    }
}

void Music::after_restore(const HostSettings& host) {
    // The settings come from the save, the songs are those of the game that
    // ran before; the reverbs and the counter are the host's in ScummVM, and
    // what the clearing changes in them stays. ScummVM saves the driver's
    // reverb (MidiPlayer::getReverb) and restores the global reverb from it.
    std::vector<std::unique_ptr<MusicEntry>> restored = std::move(play_list_);
    play_list_ = std::move(previous_play_list_);
    previous_play_list_.clear();
    const std::int8_t saved_reverb = driver_reverb_;
    driver_reverb_ = host.driver_reverb;
    global_reverb_ = host.global_reverb;
    time_counter_ = host.time_counter;
    const bool needs_resume = needs_resume_;
    const int global_pause = global_pause_;
    set_sound_on(sound_on_);
    set_master_volume(master_volume_);
    set_global_reverb(saved_reverb);
    clear_play_list();
    play_list_ = std::move(restored);
    // MusicEntry::time is not saved: a restored song's is 0 until
    // soundInitSnd numbers it.
    for (const auto& sound : play_list_) {
        sound->time = 0;
    }
    needs_resume_ = needs_resume;
    global_pause_ = global_pause;
}

void Music::set_global_reverb(std::int8_t reverb) {
    const MusicEntry* const playing = first_slot_with_status(SoundStatus::Playing);
    if (reverb != 127) {
        global_reverb_ = reverb;
        if (playing != nullptr && playing->reverb == 127) {
            set_driver_reverb(reverb);
        }
    } else if (playing != nullptr) {
        set_driver_reverb(playing->reverb);
    }
}

void Music::drop_previous_play_list() {
    for (auto& entry : previous_play_list_) {
        if (entry->parser && entry->status == SoundStatus::Playing) {
            entry->parser->stop();
        }
    }
    previous_play_list_.clear();
}

void Music::stop_all() {
    for (const auto& sound : play_list_) {
        sound_stop(*sound);
    }
}

MusicEntry& Music::add(std::unique_ptr<MusicEntry> sound) {
    MusicEntry& added = *sound;
    play_list_.push_back(std::move(sound));
    sort_play_list();
    return added;
}

MusicEntry* Music::slot(Reg object) {
    for (const auto& sound : play_list_) {
        if (sound->sound_object == object) {
            return sound.get();
        }
    }
    return nullptr;
}

MusicEntry* Music::first_slot_with_status(SoundStatus status) {
    for (const auto& sound : play_list_) {
        if (sound->status == status) {
            return sound.get();
        }
    }
    return nullptr;
}

void Music::sort_play_list() {
    sort(0, play_list_.size());
}

// Common::sort: a quicksort with the middle element as pivot. Its order of
// equal elements is kept, as the playlist order is visible to scripts.
void Music::sort(std::size_t first, std::size_t last) { // NOLINT(misc-no-recursion): a quicksort.
    if (first == last) {
        return;
    }
    std::size_t pivot = first + ((last - first) / 2);
    // sortPartition.
    const std::size_t end = last - 1;
    if (pivot != end) {
        std::swap(play_list_[pivot], play_list_[end]);
    }
    std::size_t sorted = first;
    for (std::size_t i = first; i != end; ++i) {
        if (!plays_before(*play_list_[end], *play_list_[i])) {
            if (i != sorted) {
                std::swap(play_list_[i], play_list_[sorted]);
            }
            ++sorted;
        }
    }
    if (end != sorted) {
        std::swap(play_list_[end], play_list_[sorted]);
    }
    pivot = sorted;
    sort(first, pivot);
    sort(pivot + 1, last);
}

void Music::sound_init(MusicEntry& sound) {
    // SciMusic::soundInitSnd, SCI0: one track; with digital sound effects
    // preferred (ScummVM's default), a sample in it would play instead.
    sound.time = ++time_counter_;
    if (!sound.resource) {
        return; // Only sounds with a resource are initialised.
    }
    const SoundResource& resource = *sound.resource;
    if (resource.has_digital_sample) {
        faults_->raise(std::format("Sound {}: digital samples are not supported", sound.resource_id));
        return;
    }
    if (!sound.parser) {
        sound.parser = std::make_unique<MidiParser>(*this);
        sound.parser->set_timer_rate(tempo_);
        sound.parser->set_master_volume(static_cast<std::uint8_t>(master_volume_));
    }
    sound.pause_counter = 0;
    const audio::PlayerChoices choices = player_choices();
    const unsigned channel_mask = resource.channel_filter_mask(choices.play_id, choices.has_rhythm_channel);
    // Looping and hold are off while the song loads.
    const std::uint16_t previous_loop = sound.loop;
    const std::int16_t previous_hold = sound.hold;
    sound.loop = 0;
    sound.hold = -1;
    sound.play_bed = false;
    sound.override_priority = false;
    sound.parser->load_music(resource.song, sound, channel_mask);
    sound.reverb = 127; // MidiParser_SCI::getSongReverb for SCI0.
    sound.loop = previous_loop;
    sound.hold = previous_hold;
}

// Stopping a sound resumes a paused one, which plays it.
// NOLINTNEXTLINE(readability-function-cognitive-complexity,misc-no-recursion): kept close to SciMusic::soundPlay.
void Music::sound_play(MusicEntry& sound, bool restoring) {
    if (sound.play_bed) {
        for (const auto& other : play_list_) {
            if (other.get() != &sound && other->play_bed) {
                sound_stop(*other);
                break;
            }
        }
    }
    bool found = false;
    MusicEntry* already_playing = nullptr;
    for (const auto& entry : play_list_) {
        if (entry.get() == &sound) {
            found = true;
        }
        if (entry->status == SoundStatus::Playing && entry->parser) {
            already_playing = entry.get();
        }
    }
    if (!found) {
        faults_->raise("soundPlay: the sound is not in the playlist");
        return;
    }
    sound.time = ++time_counter_;
    sort_play_list();

    if (sound.parser && already_playing != nullptr && !restoring) {
        // SCI0 plays one song at a time: a song of higher priority pauses
        // the one playing, others wait (updateSci0Cues resumes them).
        if (sound.priority > already_playing->priority) {
            sound_pause(*already_playing);
        } else {
            sound.status = SoundStatus::Paused;
            return;
        }
    }
    if (sound.parser) {
        if (sound.status != SoundStatus::Paused) {
            sound.parser->send_init_commands();
        }
        // Always, also for a paused song (ScummVM bug #11476).
        sound.parser->init_track();
        sound.parser->set_volume(static_cast<std::uint8_t>(sound.volume));
        const std::uint16_t previous_loop = sound.loop;
        const std::int16_t previous_hold = sound.hold;
        sound.loop = 0;
        sound.hold = -1;
        const bool fast_forward =
            sound.status == SoundStatus::Paused || (sound.status == SoundStatus::Playing && restoring);
        if (!fast_forward) {
            sound.parser->jump_to_tick(0);
        } else {
            sound.parser->jump_to_tick(sound.ticker, true, true, true);
        }
        sound.loop = previous_loop;
        sound.hold = previous_hold;
    }
    sound.status = SoundStatus::Playing;
}

void Music::sound_stop(MusicEntry& sound) { // NOLINT(misc-no-recursion): see sound_play.
    const SoundStatus previous = sound.status;
    sound.status = SoundStatus::Stopped;
    if (sound.parser && previous == SoundStatus::Playing) {
        sound.parser->stop();
    }
    sound.fade_step = 0;
    // SSCI0 resumes the first paused sound of the (priority ordered) list.
    if (previous == SoundStatus::Playing) {
        if (MusicEntry* paused = first_slot_with_status(SoundStatus::Paused)) {
            sound_resume(*paused);
        }
    }
}

void Music::sound_kill(MusicEntry& sound) {
    sound.status = SoundStatus::Stopped;
    if (sound.parser) {
        sound.parser->unload_music();
        sound.parser.reset();
    }
    const auto found = std::ranges::find_if(
        play_list_, [&](const std::unique_ptr<MusicEntry>& entry) { return entry.get() == &sound; });
    if (found != play_list_.end()) {
        play_list_.erase(found);
    }
}

void Music::pause_all(bool pause) {
    const bool already_unpaused = global_pause_ <= 0;
    global_pause_ += pause ? 1 : -1;
    const bool still_unpaused = global_pause_ <= 0;
    // The counter may go below 0 (ScummVM's autosave); the songs are only
    // changed when it crosses 0 or goes above 1.
    if (already_unpaused && still_unpaused) {
        return;
    }
    // Over the live list, as ScummVM's iterator goes: resuming may sort the
    // list in place, and the loop then goes on in the new order.
    for (const auto& song : play_list_) {
        if (pause) {
            sound_pause(*song);
        } else {
            sound_resume(*song);
        }
    }
}

void Music::sound_pause(MusicEntry& sound) {
    ++sound.pause_counter;
    if (sound.status != SoundStatus::Playing) {
        return;
    }
    needs_resume_ = true;
    sound.status = SoundStatus::Paused;
    if (sound.parser) {
        sound.parser->pause();
    }
}

void Music::sound_resume(MusicEntry& sound) { // NOLINT(misc-no-recursion): see sound_play.
    if (sound.pause_counter > 0) {
        --sound.pause_counter;
    }
    if (sound.pause_counter != 0) {
        return;
    }
    if (sound.status != SoundStatus::Paused || (global_pause_ > 0 && !needs_resume_)) {
        return;
    }
    needs_resume_ = false;
    sound_play(sound, true);
}

void Music::sound_set_volume(MusicEntry& sound, int volume) {
    if (volume > music_volume_max) {
        // An assertion in ScummVM.
        faults_->raise(std::format("soundSetVolume: volume {} out of range", volume));
        return;
    }
    if (sound.parser) {
        sound.parser->set_volume(static_cast<std::uint8_t>(volume));
    }
}

void Music::sound_set_priority(MusicEntry& sound, std::uint8_t priority) {
    sound.priority = priority;
    sound.time = ++time_counter_;
    sort_play_list();
}

void Music::set_master_volume(std::uint16_t volume) {
    master_volume_ = volume;
    for (const auto& sound : play_list_) {
        if (sound->parser) {
            sound->parser->set_master_volume(static_cast<std::uint8_t>(volume));
        }
    }
}

} // namespace sci::vm
