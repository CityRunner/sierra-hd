// SPDX-License-Identifier: GPL-3.0-or-later
//
// Renders one SCI0 sound resource through the music player and the AdLib
// driver, outside the VM: the player's timer runs at the driver's rate
// (250 calls a second) and after each call the OPL chip generates the
// samples up to the next one, as ScummVM's OPL stream calls its timer
// callback and then generates (audio/chip.cpp, EmulatedChip::readBuffer).
// The output is the chip's at Opl3::native_rate, a pure function of the
// inputs: what the OPL golden hashes check.
//
// The same on the MT-32 driver: after each timer call the synthesiser
// renders the frames up to the next one at Mt32::native_rate (128 a call),
// the messages of the call taking effect at its first frame. Its output
// also depends on rand()'s state (docs/ASSUMPTIONS.md, "Milestone 7:
// mt32emu") and is not hashed.

#pragma once

#include "sci/audio/adlib.hpp"
#include "sci/audio/mt32_driver.hpp"
#include "sci/core/error.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace sci::resource {
class ResourceManager;
}

namespace sci::vm {

struct SoundRenderOptions {
    // At most this many stereo frames (at the device's native rate).
    std::uint32_t max_frames = 0;
    // The sound object's loop count: 1 plays once.
    std::uint16_t loop = 1;
    // kDoSound's master volume, 0-15; 11 is the game's start value
    // (docs/ASSUMPTIONS.md, "Sound").
    std::uint16_t master_volume = 11;
    // Called with every register write the driver makes and the number of
    // music timer calls made so far, as sci-vm-trace --opl writes them
    // (docs/formats/replay-and-trace.md, "OPL files"); the driver's start-up
    // writes are seen only by the overload that creates the driver.
    // NOLINTNEXTLINE(readability-redundant-member-init): designated initializers may leave it out.
    std::function<void(std::uint32_t timer_call, std::uint16_t reg, std::uint8_t value)> on_opl = {};
};

// What the scripts would see of a playing sound after a timer call: a
// signal (taken from the sound, as kDoSound's update of the cues does) or
// a new cumulative cue.
struct SoundEvent {
    std::uint32_t timer_call = 0;
    std::uint16_t signal = 0;
    std::uint16_t cue = 0;

    friend bool operator==(const SoundEvent&, const SoundEvent&) = default;
};

struct RenderedSound {
    // Interleaved left and right samples at the device's native rate.
    std::vector<std::int16_t> samples;
    // The signals and cues, in order; they do not depend on the device.
    std::vector<SoundEvent> events;
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

// Plays `sound` on the MT-32 driver, rendering at audio::Mt32::native_rate
// (options.on_opl is not called).
[[nodiscard]] core::Result<RenderedSound> render_sound(std::span<const std::uint8_t> sound, int number,
                                                       audio::Mt32Driver& driver, const SoundRenderOptions& options);

// The same for sound resource `number` of a game, on a new MT-32 driver
// opened on the ROMs with the game's patch.001 (audio::load_mt32_patch).
[[nodiscard]] core::Result<RenderedSound> render_sound_mt32(const resource::ResourceManager& resources,
                                                            std::uint16_t number,
                                                            std::span<const std::uint8_t> control_rom,
                                                            std::span<const std::uint8_t> pcm_rom,
                                                            const SoundRenderOptions& options);

} // namespace sci::vm
