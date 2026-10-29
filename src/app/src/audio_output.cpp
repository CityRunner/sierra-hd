// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio_output.hpp"

#include "sci/audio/opl3.hpp"
#include "sci/core/log.hpp"

#include <SDL3/SDL_init.h>
#include <algorithm>
#include <atomic>
#include <format>
#include <utility>

namespace sci::app {

namespace {

constexpr int channels = 2;
constexpr std::size_t frame_bytes = sizeof(std::int16_t) * channels;

// On SDL's audio thread, with the stream locked: counts what the device
// wanted beyond what was queued (it plays silence for it).
void SDLCALL count_underrun(void* starved_bytes, SDL_AudioStream* /*stream*/, int additional_amount,
                            int /*total_amount*/) {
    if (additional_amount > 0) {
        static_cast<std::atomic<std::uint64_t>*>(starved_bytes)
            ->fetch_add(static_cast<std::uint64_t>(additional_amount), std::memory_order_relaxed);
    }
}

void warn(std::string_view what) {
    core::log(core::LogLevel::Warning, core::LogCategory::General,
              std::format("{}: {}; the music plays without sound", what, SDL_GetError()));
}

} // namespace

std::unique_ptr<AudioOutput> AudioOutput::open(std::vector<audio::AdlibPatch> patches, double volume) {
    auto driver = std::make_unique<audio::AdlibDriver>(std::move(patches));
    audio::AdlibDriver& adlib = *driver;
    return open(
        Synth{
            .driver = std::move(driver),
            .rate = audio::Opl3::native_rate,
            .render = [&adlib](std::span<std::int16_t> samples) { adlib.opl().generate(samples); },
        },
        volume);
}

std::unique_ptr<AudioOutput> AudioOutput::open(std::unique_ptr<audio::Mt32Driver> driver, double volume) {
    audio::Mt32Driver& mt32 = *driver;
    return open(
        Synth{
            .driver = std::move(driver),
            .rate = audio::Mt32::native_rate,
            .render = [&mt32](std::span<std::int16_t> samples) { mt32.synth().render(samples); },
        },
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
    if (!SDL_SetAudioStreamGetCallback(stream, count_underrun, &output->starved_bytes_)) {
        warn("Cannot watch the audio queue");
        return nullptr;
    }
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
    put_silence(target_frames_);
}

AudioOutput::~AudioOutput() {
    if (recoveries_ != 0 || dropped_frames_ != 0) {
        core::log(core::LogLevel::Info, core::LogCategory::General,
                  std::format("audio: {} underruns made up for with {} ms of silence, {} ms dropped", recoveries_,
                              recovery_frames_ * 1000 / rate_, dropped_frames_ * 1000 / rate_));
    }
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
    if (fast_ && dropping_ && queued_frames() > target_frames_) {
        dropped_frames_ += (next - synth_frames_) * rate_ / synth_.rate;
        synth_frames_ = next;
        return;
    }
    synth_samples_.resize(static_cast<std::size_t>(next - synth_frames_) * channels);
    synth_frames_ = next;
    synth_.render(synth_samples_);
    const std::size_t queued = queued_frames();
    mixer_.set_speed(audio::queue_speed(queued, target_frames_));
    output_samples_.clear();
    mixer_.process(synth_samples_, output_samples_);
    // Too far ahead of the device (after a stall the interpreter caught up
    // on): drop rather than let the delay grow, until the queue is back at
    // the target, so that the music is cut once rather than at every call
    // around the limit.
    const std::size_t frames = output_samples_.size() / channels;
    if (queued + frames > target_frames_ * 2) {
        dropping_ = true;
    } else if (queued + frames <= target_frames_) {
        dropping_ = false;
    }
    if (dropping_) {
        dropped_frames_ += frames;
        return;
    }
    put(output_samples_);
}

bool AudioOutput::recover() {
    // A short underrun (a few frames of jitter with the queue low) is left
    // to the speed correction: topping up would turn a click into a gap.
    const std::uint64_t starved = starved_bytes_.exchange(0, std::memory_order_relaxed) / frame_bytes;
    const std::size_t queued = queued_frames();
    if (starved < target_frames_ / 4 || queued >= target_frames_) {
        return false;
    }
    ++recoveries_;
    recovery_frames_ += target_frames_ - queued;
    put_silence(target_frames_ - queued);
    return true;
}

std::size_t AudioOutput::queued_frames() const {
    const int bytes = SDL_GetAudioStreamQueued(stream_);
    return bytes > 0 ? static_cast<std::size_t>(bytes) / frame_bytes : 0;
}

void AudioOutput::put_silence(std::size_t frames) {
    const std::vector<std::int16_t> silence(frames * channels, 0);
    put(silence);
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
