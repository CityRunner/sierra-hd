// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/state.cpp, engines/sci/sci.cpp, common/random.cpp
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

#include "timing.hpp"

#include <algorithm>

namespace sci::vm {

void Clock::set_music_timer(std::function<void()> timer, std::uint32_t tempo_microseconds) {
    music_timer_ = std::move(timer);
    tempo_ = tempo_microseconds;
    music_due_ = microseconds_ + tempo_microseconds;
}

LocalTime Clock::local_time() const {
    if (local_time_) {
        return local_time_();
    }
    return deterministic_local_time(microseconds_);
}

void Clock::read_elapsed(std::uint64_t call) {
    // Calls count from 1, so 0 means no read yet.
    if (last_elapsed_read_ != 0 && call == last_elapsed_read_ + 1) {
        advance(1);
    }
    last_elapsed_read_ = call;
}

Clock::HostTime Clock::host_time() const {
    return HostTime{
        .microseconds = microseconds_,
        .wait_start = wait_start_,
        .wait_end = wait_end_,
        .music_due = music_due_,
        .tempo = tempo_,
    };
}

void Clock::after_restore(const HostTime& host) {
    const std::uint32_t play_ticks = this->play_ticks();
    microseconds_ = host.microseconds;
    wait_start_ = host.wait_start;
    wait_end_ = host.wait_end;
    music_due_ = host.music_due;
    tempo_ = host.tempo;
    set_play_ticks(play_ticks);
    last_wait_ = millis();
    throttle_last_ = 0;
    throttle_trigger_ = false;
}

void Clock::advance(std::uint32_t milliseconds) {
    wait_start_ = microseconds_;
    wait_end_ = microseconds_ + (static_cast<std::uint64_t>(milliseconds) * 1000);
    if (wait_hook_) {
        wait_hook_(milliseconds);
    }
    microseconds_ = wait_end_;
    run_music_timer(microseconds_);
    if (advance_hook_) {
        advance_hook_();
    }
}

void Clock::run_music_in_wait(std::uint32_t milliseconds) {
    run_music_timer(std::min(wait_start_ + (static_cast<std::uint64_t>(milliseconds) * 1000), wait_end_));
}

void Clock::run_music_timer(std::uint64_t until) {
    while (music_timer_ && tempo_ != 0 && music_due_ <= until) {
        music_due_ += tempo_;
        music_timer_();
    }
}

void Clock::sleep(std::uint32_t milliseconds) {
    if (milliseconds == 0) {
        return;
    }
    if (sleep_poll_) {
        sleep_poll_();
    }
    advance(milliseconds);
}

std::uint16_t Clock::wait(std::uint16_t ticks) {
    std::uint32_t time = millis();
    const std::uint32_t ms = ticks * 1000U / 60U;
    const std::uint32_t duration = time - last_wait_;
    if (ms > duration) {
        const std::uint32_t sleep_time = ms - duration;
        sleep(sleep_time);
        time += sleep_time;
    }
    const auto delta = static_cast<std::uint16_t>((static_cast<std::int64_t>(time) - last_wait_) * 60 / 1000);
    last_wait_ = time;
    return delta;
}

void Clock::speed_throttler(std::uint32_t needed_sleep) {
    if (throttle_trigger_) {
        const std::uint32_t now = millis();
        const std::uint32_t duration = now - throttle_last_;
        if (duration < needed_sleep) {
            sleep(needed_sleep - duration);
            throttle_last_ = millis();
        } else {
            throttle_last_ = now;
        }
        throttle_trigger_ = false;
    }
}

std::uint32_t RandomSource::next(std::uint32_t max) {
    seed_ ^= seed_ >> 13U;
    seed_ ^= seed_ << 21U;
    seed_ ^= seed_ >> 11U;
    const std::uint32_t value = seed_ * 0xDEADBF03U;
    if (max == 0xFFFFFFFFU) {
        return value;
    }
    return value % (max + 1);
}

} // namespace sci::vm
