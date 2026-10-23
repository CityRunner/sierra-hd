// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 sound commands (kDoSound) between the scripts' Sound objects and
// the music player, with the silent driver of spec section 8. Ported from
// ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/sound/soundcmd.{h,cpp} (SoundCommandParser, SCI0 paths)
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
#include "sci/vm/reg.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace sci::vm {

class Machine;
class Music;

class Sound {
public:
    Sound();
    ~Sound();
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;
    Sound(Sound&&) = delete;
    Sound& operator=(Sound&&) = delete;

    // Opens the music player (SciEngine::run creating the
    // SoundCommandParser, then syncSoundSettings) and puts its timer on the
    // virtual clock.
    void attach(Machine& machine);
    // The device the music plays on, set before attach (as ScummVM opens
    // the driver before syncSoundSettings sets the volume); null is the
    // silent driver. It must outlive the player.
    void set_device(audio::MidiDevice* device) { device_ = device; }
    // SciEngine::runGame, when the game ends or restarts.
    void clear_play_list();
    // Passes signals and cues of the playing sounds to their objects; SCI0
    // kGetEvent calls it.
    void update_sci0_cues(Machine& machine);

    // The kDoSound subfunctions; `args` excludes the subfunction number.
    Reg init(Machine& machine, std::span<Reg> args);
    Reg play(Machine& machine, std::span<Reg> args);
    Reg dispose(Machine& machine, std::span<Reg> args);
    Reg mute(Machine& machine, std::span<Reg> args);
    Reg stop(Machine& machine, std::span<Reg> args);
    Reg pause(Machine& machine, std::span<Reg> args);
    Reg master_volume(Machine& machine, std::span<Reg> args);
    Reg update(Machine& machine, std::span<Reg> args);
    Reg fade(Machine& machine, std::span<Reg> args);
    static Reg polyphony(Machine& machine, std::span<Reg> args);

    // For tests and tools: the music player.
    [[nodiscard]] Music& music() { return *music_; }

private:
    void process_init_sound(Machine& machine, Reg object);
    void process_play_sound(Machine& machine, Reg object);
    void process_dispose_sound(Machine& machine, Reg object);
    void process_stop_sound(Machine& machine, Reg object, bool sample_finished_playing);
    void process_update_cues(Machine& machine, Reg object);

    std::unique_ptr<Music> music_; // Created when attached.
    audio::MidiDevice* device_ = nullptr;
};

} // namespace sci::vm
