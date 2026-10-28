// SPDX-License-Identifier: GPL-3.0-or-later
//
// The hand-off of the music to SDL's audio thread (sci::app::AudioOutput),
// on SDL's dummy audio driver, which plays at the pace of a real device
// without one. The main thread drives the AdLib driver as the game does
// while SDL's thread pulls the samples: run under TSan (label "threads"),
// this checks that the two share nothing but SDL's locked stream.

#include "audio_output.hpp"
#include "sci/audio/adlib.hpp"
#include "sci_test.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <vector>

namespace {

// 48 instruments that sound: a sine carrier at full level, fast attack.
std::vector<sci::audio::AdlibPatch> audible_patches() {
    sci::audio::AdlibOperator op;
    op.frequency_mult = 1;
    op.attack_rate = 15;
    op.decay_rate = 4;
    op.sustain_level = 2;
    op.release_rate = 6;
    sci::audio::AdlibPatch patch;
    patch.op = {op, op};
    patch.op[0].total_level = 63; // A silent modulator: a plain sine.
    return std::vector<sci::audio::AdlibPatch>(48, patch);
}

// An SCI0 sound header: a byte for the digital sample flag, then per MIDI
// channel a voice count and a play mask; channel 0 gets the AdLib (0x04)
// and two voices.
std::array<std::uint8_t, 33> header() {
    std::array<std::uint8_t, 33> bytes{};
    bytes[1] = 2;
    bytes[2] = 0x04;
    return bytes;
}

} // namespace

SCI_TEST(audio_output_feeds_sdl_from_the_main_thread) {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    auto output = sci::app::AudioOutput::open(audible_patches(), 0.5);
    SCI_CHECK(output != nullptr);
    if (!output) {
        return;
    }
    SCI_CHECK(output->rate() > 0);
    SCI_CHECK(output->target_frames() >= output->rate() * sci::app::AudioOutput::target_milliseconds / 1000);
    const auto song = header();
    output->init_track(song);
    // Half a second of timer calls at their real pace, with notes.
    for (int call = 0; call < 125; ++call) {
        if (call % 25 == 0) {
            output->send(0x00403090U + (static_cast<std::uint32_t>(call / 25) << 8U)); // Note on, channel 0.
        }
        output->on_timer();
        SDL_Delay(4);
    }
    // The main thread never ran ahead of the device by more than the bound.
    SCI_CHECK(output->dropped_frames() == 0);
    output.reset();
    SDL_Quit();
}
