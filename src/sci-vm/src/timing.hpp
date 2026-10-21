// SPDX-License-Identifier: GPL-3.0-or-later
//
// The virtual clock and the random source (spec section 7). Time passes
// only where the interpreter sleeps or waits, as in the deterministic mode
// of the reference (tools/scummvm-patches/0003-deterministic-trace.patch);
// the app advances it with real time between frames. Ported from ScummVM
// (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/state.cpp (EngineState::wait, speedThrottler),
//   engines/sci/sci.cpp (SciEngine::sleep, getTickCount),
//   common/random.cpp (RandomSource)
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

#include <cstdint>
#include <functional>

namespace sci::vm {

class Clock {
public:
    // Called once per `tempo` microseconds of virtual time (the music
    // timer), and when sleeping, before time passes (the event poll).
    void set_music_timer(std::function<void()> timer, std::uint32_t tempo_microseconds);

    void set_sleep_poll(std::function<void()> poll) { sleep_poll_ = std::move(poll); }

    // Called after time advances; may ask the interpreter to stop.
    void set_advance_hook(std::function<void()> hook) { advance_hook_ = std::move(hook); }

    // Called before time advances, with the milliseconds: the program waits
    // that long in real time there, presenting frames and reading input.
    void set_wait_hook(std::function<void(std::uint32_t)> hook) { wait_hook_ = std::move(hook); }

    [[nodiscard]] std::uint32_t millis() const { return static_cast<std::uint32_t>(microseconds_ / 1000); }

    // Game ticks: 1/60 s.
    [[nodiscard]] std::uint32_t ticks() const { return static_cast<std::uint32_t>(microseconds_ * 60 / 1000000); }

    void advance(std::uint32_t milliseconds);
    // SciEngine::sleep: the event queue is polled once, then time passes.
    void sleep(std::uint32_t milliseconds);
    // kGetTime reads the elapsed time in kernel call `call`. A script that
    // waits by reading it over and over, with no other kernel call between
    // the reads, would never see time pass on the virtual clock, so such a
    // read lets 1 ms pass first (as the reference's deterministic mode does).
    void read_elapsed(std::uint64_t call);

    // EngineState::wait: sleeps until `ticks` have passed since the last
    // wait; returns the ticks that passed.
    std::uint16_t wait(std::uint16_t ticks);
    // EngineState::speedThrottler.
    void speed_throttler(std::uint32_t needed_sleep);

    void set_throttle_trigger(bool trigger) { throttle_trigger_ = trigger; }

    [[nodiscard]] bool throttle_trigger() const { return throttle_trigger_; }

    void reset_throttle() { throttle_last_ = 0; }

    void reset_wait() { last_wait_ = millis(); }

private:
    std::uint64_t microseconds_ = 0;
    std::uint64_t music_due_ = 0;
    std::uint32_t tempo_ = 0;
    std::function<void()> music_timer_;
    std::function<void()> sleep_poll_;
    std::function<void()> advance_hook_;
    std::function<void(std::uint32_t)> wait_hook_;
    std::uint32_t last_wait_ = 0;
    std::uint64_t last_elapsed_read_ = 0;
    std::uint32_t throttle_last_ = 0;
    bool throttle_trigger_ = false;
};

// Common::RandomSource: a xorshift generator.
class RandomSource {
public:
    void set_seed(std::uint32_t seed) { seed_ = seed == 0 ? 1 : seed; }

    // A number from 0 to max.
    std::uint32_t next(std::uint32_t max);

    [[nodiscard]] std::uint32_t seed() const { return seed_; }

private:
    std::uint32_t seed_ = 1;
};

} // namespace sci::vm
