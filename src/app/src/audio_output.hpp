// SPDX-License-Identifier: GPL-3.0-or-later
//
// The music of the running game through SDL3 audio (milestone 7, spec
// sections 4.3 and 8): the music device the game's player sends to, which
// renders its output and hands it to the audio thread.
//
// Threads and clocks (docs/ASSUMPTIONS.md, "Milestone 7: mixer and audio
// output"):
// - The interpreter, the player, the music driver (AdLib or MT-32), its
//   synthesiser and the mixer all run on the main thread. After each music
//   timer call the synthesiser makes the samples up to the next one (49716
//   / 250 frames a call for the OPL chip, 32000 / 250 for the MT-32, as
//   sci::vm::render_sound does), the mixer converts them to the device's
//   rate, and they go into an SDL_AudioStream. The MIDI messages of a
//   timer call reach the MT-32 at once, so they take effect at the call's
//   first frame.
// - SDL's audio thread pulls from that stream. The stream, locked inside
//   SDL, is the only thing the two threads share; this class has no state
//   of its own that the audio thread sees.
// - The timer calls follow real time (the host runs them during its waits,
//   sci::vm::Game::pass_wait); the device plays by its own clock. The
//   queue in the stream is held near a target (queue_speed adjusts the
//   resampling by up to 0.2 %), new audio is dropped above twice the target
//   (after a stall that the interpreter then catches up on), and the device
//   plays silence when the queue runs dry.

#pragma once

#include "sci/audio/adlib.hpp"
#include "sci/audio/midi_device.hpp"
#include "sci/audio/mixer.hpp"
#include "sci/audio/mt32_driver.hpp"

#include <SDL3/SDL_audio.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace sci::app {

class AudioOutput final : public audio::MidiDevice {
public:
    // The queue the output aims for, in milliseconds (with at least three
    // of the device's buffers), so that the music plays on through the
    // host's frames (a frame is up to about 17 ms) and short pauses of the
    // interpreter.
    static constexpr std::uint32_t target_milliseconds = 60;

    // Opens SDL's audio and the default playback device, at the device's
    // own rate, with the AdLib driver on `patches` and `volume` (0 to 1).
    // Null, with a warning in the log, when there is no audio device: the
    // game then plays on the silent driver.
    [[nodiscard]] static std::unique_ptr<AudioOutput> open(std::vector<audio::AdlibPatch> patches, double volume);
    // The same with the MT-32 driver, rendering at audio::Mt32::native_rate.
    [[nodiscard]] static std::unique_ptr<AudioOutput> open(std::unique_ptr<audio::Mt32Driver> driver, double volume);

    ~AudioOutput() override;
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;
    AudioOutput(AudioOutput&&) = delete;
    AudioOutput& operator=(AudioOutput&&) = delete;

    [[nodiscard]] audio::PlayerChoices player_choices() const override;
    void send(std::uint32_t message) override;
    void init_track(std::span<const std::uint8_t> header) override;
    void set_volume(std::uint8_t volume) override;
    void play_switch(bool play) override;
    void sysex(std::span<const std::uint8_t> message) override;
    void set_reverb(std::int8_t reverb) override;
    // The driver's step, then the samples up to the next timer call.
    void on_timer() override;

    // The output rate and the queue target, in frames.
    [[nodiscard]] std::uint32_t rate() const { return rate_; }

    [[nodiscard]] std::size_t target_frames() const { return target_frames_; }

    // Frames dropped because the queue was full (for tests and the log).
    [[nodiscard]] std::uint64_t dropped_frames() const { return dropped_frames_; }

private:
    // The driver, and what makes its output at `rate`.
    struct Synth {
        std::unique_ptr<audio::MidiDevice> driver;
        std::uint32_t rate = 0;
        std::function<void(std::span<std::int16_t>)> render;
    };

    [[nodiscard]] static std::unique_ptr<AudioOutput> open(Synth synth, double volume);
    AudioOutput(SDL_AudioStream* stream, std::uint32_t rate, std::size_t device_frames, Synth synth, double volume);

    // Frames now waiting in the stream.
    [[nodiscard]] std::size_t queued_frames() const;
    void put(std::span<const std::int16_t> frames);

    SDL_AudioStream* stream_;
    std::uint32_t rate_;
    std::size_t target_frames_;
    Synth synth_;
    audio::Mixer mixer_;
    std::uint64_t timer_calls_ = 0;
    std::uint64_t synth_frames_ = 0;
    std::uint64_t dropped_frames_ = 0;
    std::vector<std::int16_t> synth_samples_;
    std::vector<std::int16_t> output_samples_;
};

} // namespace sci::app
