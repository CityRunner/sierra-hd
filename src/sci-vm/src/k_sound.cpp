// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/ksound.cpp (kDoSound and its SCI0 subfunctions)
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

#include "kernel_functions.hpp"
#include "machine.hpp"
#include "sound.hpp"
#include "subsystems.hpp"

namespace sci::vm {

Reg k_do_sound(Machine& machine, std::span<Reg> /*args*/) {
    // Only its subfunctions are called.
    machine.faults().raise("kDoSound: not supposed to call this");
    return machine.acc();
}

Reg k_do_sound_init(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().init(machine, args);
}

Reg k_do_sound_play(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().play(machine, args);
}

Reg k_do_sound_dispose(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().dispose(machine, args);
}

Reg k_do_sound_mute(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().mute(machine, args);
}

Reg k_do_sound_stop(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().stop(machine, args);
}

Reg k_do_sound_pause(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().pause(machine, args);
}

Reg k_do_sound_resume_after_restore(Machine& machine, // NOLINT(misc-const-correctness): the kernel signature.
                                    std::span<Reg> /*args*/) {
    // Called after restoring a game to start the music again; ScummVM's
    // restore does that itself.
    return machine.acc();
}

Reg k_do_sound_master_volume(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().master_volume(machine, args);
}

Reg k_do_sound_update(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().update(machine, args);
}

Reg k_do_sound_fade(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().fade(machine, args);
}

Reg k_do_sound_get_polyphony(Machine& machine, std::span<Reg> args) {
    return machine.subsystems().sound().polyphony(machine, args);
}

Reg k_do_sound_stop_all(Machine& machine, // NOLINT(misc-const-correctness): the kernel signature.
                        std::span<Reg> /*args*/) {
    // Disabled in ScummVM: it would stop the background sounds of KQ1.
    return machine.acc();
}

} // namespace sci::vm
