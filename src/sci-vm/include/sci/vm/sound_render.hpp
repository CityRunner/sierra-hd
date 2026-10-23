// SPDX-License-Identifier: GPL-3.0-or-later
//
// Renders one SCI0 sound resource through the music player and the AdLib
// driver, outside the VM: the player's timer runs at the driver's rate
// (250 calls a second) and after each call the OPL chip generates the
// samples up to the next one, as ScummVM's OPL stream calls its timer
// callback and then generates (audio/chip.cpp, EmulatedChip::readBuffer).
// The output is the chip's at Opl3::native_rate, a pure function of the
// inputs: what the OPL golden hashes check.

#pragma once

#include "sci/audio/adlib.hpp"
#include "sci/core/error.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::resource {
class ResourceManager;
}

namespace sci::vm {

struct SoundRenderOptions {
    // At most this many stereo frames (at Opl3::native_rate).
    std::uint32_t max_frames = 0;
    // The sound object's loop count: 1 plays once.
    std::uint16_t loop = 1;
    // kDoSound's master volume, 0-15; 11 is the game's start value
    // (docs/ASSUMPTIONS.md, "Sound").
    std::uint16_t master_volume = 11;
};

struct RenderedSound {
    // Interleaved left and right samples at Opl3::native_rate.
    std::vector<std::int16_t> samples;
    // Music timer calls made.
    std::uint32_t timer_calls = 0;
    // The song reached its end before max_frames.
    bool ended = false;
};

// Plays `sound` (the data of SCI0 sound resource `number`) from its start
// on `driver`, until the song ends or max_frames frames are out.
[[nodiscard]] core::Result<RenderedSound> render_sound(std::span<const std::uint8_t> sound, int number,
                                                       audio::AdlibDriver& driver, const SoundRenderOptions& options);

// The same for sound resource `number` of a game, on a new AdLib driver
// with the game's patches (audio::load_adlib_patches).
[[nodiscard]] core::Result<RenderedSound> render_sound(const resource::ResourceManager& resources, std::uint16_t number,
                                                       const SoundRenderOptions& options);

} // namespace sci::vm
