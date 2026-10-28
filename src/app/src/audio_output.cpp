// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio_output.hpp"

#include "sci/audio/opl3.hpp"
#include "sci/core/log.hpp"

#include <SDL3/SDL_init.h>
#include <algorithm>
#include <format>
#include <utility>

namespace sci::app {

namespace {

constexpr int channels = 2;
constexpr std::size_t frame_bytes = sizeof(std::int16_t) * channels;

void warn(std::string_view what) {
    core::log(core::LogLevel::Warning, core::LogCategory::General,
              std::format("{}: {}; the music plays without sound", what, SDL_GetError()));
}

} // namespace

std::unique_ptr<AudioOutput> AudioOutput::open(std::vector<audio::AdlibPatch> patches, double volume) {
    auto driver = std::make_unique<audio::AdlibDriver>(std::move(patches));
    audio::AdlibDriver& adlib = *driver;
    return open(Synth{.driver = std::move(driver),
                      .rate = audio::Opl3::native_rate,
                      .render = [&adlib](std::span<std::int16_t> samples) { adlib.opl().generate(samples); }},
                volume);
}

std::unique_ptr<AudioOutput> AudioOutput::open(std::unique_ptr<audio::Mt32Driver> driver, double volume) {
    audio::Mt32Driver& mt32 = *driver;
    return open(Synth{.driver = std::move(driver),
                      .rate = audio::Mt32::native_rate,
                      .render = [&mt32](std::span<std::int16_t> samples) { mt32.synth().render(samples); }},
                volume);
}

std::unique_ptr<AudioOutput> AudioOutput::open(Synth synth, double volume) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        warn("Cannot start the audio");
        return nullptr;
    }
    // The device's own rate, so that SDL converts nothing; its buffer size
    // sets the smallest queue.
    SDL_AudioSpec device{};
    int device_frames = 0;
    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device, &device_frames) || device.freq <= 0) {
        warn("No audio device");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return nullptr;
    }
    const SDL_AudioSpec spec{.format = SDL_AUDIO_S16, .channels = channels, .freq = device.freq};
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream == nullptr) {
        warn("Cannot open the audio device");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return nullptr;
    }
    std::unique_ptr<AudioOutput> output(new AudioOutput(stream, static_cast<std::uint32_t>(device.freq),
                                                        static_cast<std::size_t>(std::max(device_frames, 0)),
                                                        std::move(synth), volume));
    // The device starts paused; it plays the silence queued until the
    // music comes.
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        warn("Cannot start the audio device");
        return nullptr;
    }
    core::log(core::LogLevel::Info, core::LogCategory::General,
              std::format("audio: {} Hz, device buffer {} frames, queue {} frames", output->rate_, device_frames,
                          output->target_frames_));
    return output;
}

AudioOutput::AudioOutput(SDL_AudioStream* stream, std::uint32_t rate, std::size_t device_frames, Synth synth,
                         double volume)
    : stream_(stream), rate_(rate),
      target_frames_(std::max<std::size_t>(std::size_t{rate} * target_milliseconds / 1000, device_frames * 3)),
      synth_(std::move(synth)), mixer_(synth_.rate, rate) {
    mixer_.set_volume(volume);
    // Start with the target queue of silence, so that the device does not
    // run dry before the first timer calls.
    const std::vector<std::int16_t> silence(target_frames_ * channels, 0);
    put(silence);
}

AudioOutput::~AudioOutput() {
    // Stops SDL's audio thread using the stream and closes the device.
    SDL_DestroyAudioStream(stream_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

audio::PlayerChoices AudioOutput::player_choices() const {
    return synth_.driver->player_choices();
}

void AudioOutput::send(std::uint32_t message) {
    synth_.driver->send(message);
}

void AudioOutput::init_track(std::span<const std::uint8_t> header) {
    synth_.driver->init_track(header);
}

void AudioOutput::set_volume(std::uint8_t volume) {
    synth_.driver->set_volume(volume);
}

void AudioOutput::play_switch(bool play) {
    synth_.driver->play_switch(play);
}

void AudioOutput::sysex(std::span<const std::uint8_t> message) {
    synth_.driver->sysex(message);
}

void AudioOutput::set_reverb(std::int8_t reverb) {
    synth_.driver->set_reverb(reverb);
}

void AudioOutput::on_timer() {
    synth_.driver->on_timer();
    ++timer_calls_;
    // The frames up to the next timer call, without drift (as
    // sci::vm::render_sound): the OPL chip's output is the deterministic
    // one. Both drivers run the timer at 250 calls a second.
    static_assert(audio::AdlibDriver::timer_frequency == audio::Mt32Driver::timer_frequency);
    const std::uint64_t next = timer_calls_ * synth_.rate / audio::AdlibDriver::timer_frequency;
    synth_samples_.resize(static_cast<std::size_t>(next - synth_frames_) * channels);
    synth_frames_ = next;
    synth_.render(synth_samples_);
    const std::size_t queued = queued_frames();
    mixer_.set_speed(audio::queue_speed(queued, target_frames_));
    output_samples_.clear();
    mixer_.process(synth_samples_, output_samples_);
    // Too far ahead of the device (after a stall the interpreter caught up
    // on): drop rather than let the delay grow.
    if (queued + (output_samples_.size() / channels) > target_frames_ * 2) {
        dropped_frames_ += output_samples_.size() / channels;
        return;
    }
    put(output_samples_);
}

std::size_t AudioOutput::queued_frames() const {
    const int bytes = SDL_GetAudioStreamQueued(stream_);
    return bytes > 0 ? static_cast<std::size_t>(bytes) / frame_bytes : 0;
}

void AudioOutput::put(std::span<const std::int16_t> frames) {
    if (frames.empty()) {
        return;
    }
    if (!SDL_PutAudioStreamData(stream_, frames.data(), static_cast<int>(frames.size_bytes()))) {
        core::log(core::LogLevel::Warning, core::LogCategory::General,
                  std::format("audio: cannot queue samples: {}", SDL_GetError()));
    }
}

} // namespace sci::app
