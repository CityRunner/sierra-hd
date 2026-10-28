// SPDX-License-Identifier: GPL-3.0-or-later
//
// The software mixer (sci::audio::Mixer): rate conversion, volume and
// clipping. Its output is not hashed (spec section 8), so the checks are on
// lengths and on how close the output is to the ideal signal.

#include "sci/audio/mixer.hpp"
#include "sci/audio/opl3.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

using sci::audio::Mixer;

namespace {

// The sine by its Taylor series, as std::sin is not used (spec section
// 5.3): `x` reduced to [-pi, pi], where 20 terms are exact to rounding.
double sine_of(double x) {
    constexpr double two_pi = 2.0 * std::numbers::pi;
    x -= two_pi * std::floor((x + std::numbers::pi) / two_pi);
    double term = x;
    double sum = x;
    for (int k = 1; k < 20; ++k) {
        term *= -x * x / static_cast<double>((2 * k) * ((2 * k) + 1));
        sum += term;
    }
    return sum;
}

// A stereo sine of `frequency` Hz at `rate`, amplitude `amplitude`, on both
// sides (the right one inverted, to tell the sides apart).
std::vector<std::int16_t> sine(double frequency, std::uint32_t rate, std::size_t frames, double amplitude) {
    std::vector<std::int16_t> samples;
    samples.reserve(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        const double value = amplitude * sine_of(2.0 * std::numbers::pi * frequency * static_cast<double>(i) /
                                                 static_cast<double>(rate));
        const auto sample = static_cast<std::int16_t>(std::floor(value + 0.5));
        samples.push_back(sample);
        samples.push_back(static_cast<std::int16_t>(-sample));
    }
    return samples;
}

// Feeds `input` in blocks of `block` frames (as the program does, one
// music timer interval at a time).
std::vector<std::int16_t> run(Mixer& mixer, const std::vector<std::int16_t>& input, std::size_t block) {
    std::vector<std::int16_t> output;
    for (std::size_t at = 0; at < input.size(); at += block * 2) {
        const std::size_t count = std::min(block * 2, input.size() - at);
        mixer.process(std::span(input).subspan(at, count), output);
    }
    return output;
}

// The largest difference between the output and the ideal sine at the
// output rate (left, and the inverted right), after the first few frames.
double max_error(const std::vector<std::int16_t>& output, double frequency, std::uint32_t output_rate,
                 double amplitude) {
    double worst = 0.0;
    for (std::size_t i = 8; i < output.size() / 2; ++i) {
        const double time = static_cast<double>(i) / static_cast<double>(output_rate);
        const double ideal = amplitude * sine_of(2.0 * std::numbers::pi * frequency * time);
        worst = std::max(worst, std::abs(static_cast<double>(output[i * 2]) - ideal));
        worst = std::max(worst, std::abs(static_cast<double>(output[(i * 2) + 1]) + ideal));
    }
    return worst;
}

constexpr std::uint32_t opl_rate = sci::audio::Opl3::native_rate;

} // namespace

SCI_TEST(mixer_converts_the_length) {
    for (const std::uint32_t output_rate : {48000U, 44100U, 22050U, 96000U}) {
        Mixer mixer(opl_rate, output_rate);
        // Ten seconds in blocks of one music timer interval (198 or 199
        // frames).
        const std::vector<std::int16_t> input(static_cast<std::size_t>(opl_rate) * 10 * 2, 0);
        const std::vector<std::int16_t> output = run(mixer, input, 199);
        const auto frames = static_cast<std::int64_t>(output.size() / 2);
        // All but the two frames of look-ahead, within one output frame.
        const std::int64_t expected = static_cast<std::int64_t>(output_rate) * 10;
        SCI_CHECK(std::abs(frames - expected) <= 3);
    }
}

SCI_TEST(mixer_keeps_a_sine_at_48000) {
    Mixer mixer(opl_rate, 48000);
    const auto input = sine(440.0, opl_rate, opl_rate, 10000.0);
    const auto output = run(mixer, input, 199);
    SCI_CHECK(output.size() / 2 > 47990);
    // Output frame i is the input at i / 48000 s. 440 Hz is far below both
    // Nyquist frequencies: within a few steps of rounding.
    SCI_CHECK(max_error(output, 440.0, 48000, 10000.0) < 4.0);
}

SCI_TEST(mixer_keeps_a_high_sine_at_44100) {
    // 5 kHz, where linear interpolation would lose about 0.5 dB.
    Mixer mixer(opl_rate, 44100);
    const auto output = run(mixer, sine(5000.0, opl_rate, opl_rate / 2, 10000.0), 198);
    std::int16_t peak = 0;
    for (std::size_t i = 100; i < output.size(); ++i) {
        peak = std::max(peak, output[i]);
    }
    // Within 1 % of the amplitude.
    SCI_CHECK(peak > 9900 && peak <= 10100);
    SCI_CHECK(max_error(output, 5000.0, 44100, 10000.0) < 200.0);
}

SCI_TEST(mixer_keeps_silence_silent) {
    Mixer mixer(opl_rate, 48000);
    mixer.set_speed(1.002);
    const std::vector<std::int16_t> input(20000, 0);
    const auto output = run(mixer, input, 199);
    SCI_CHECK(!output.empty());
    SCI_CHECK(std::ranges::all_of(output, [](std::int16_t sample) { return sample == 0; }));
}

SCI_TEST(mixer_applies_the_volume) {
    const auto input = sine(440.0, opl_rate, 10000, 20000.0);
    Mixer full(opl_rate, 48000);
    Mixer half(opl_rate, 48000);
    half.set_volume(0.5);
    Mixer muted(opl_rate, 48000);
    muted.set_volume(0.0);
    const auto loud = run(full, input, 199);
    const auto quiet = run(half, input, 199);
    const auto none = run(muted, input, 199);
    SCI_CHECK(loud.size() == quiet.size() && loud.size() == none.size());
    for (std::size_t i = 0; i < loud.size(); ++i) {
        SCI_CHECK(std::abs((static_cast<int>(loud[i]) / 2) - quiet[i]) <= 1);
        SCI_CHECK(none[i] == 0);
    }
}

SCI_TEST(mixer_clips) {
    // A full-scale square wave overshoots when interpolated, and twice the
    // volume more so: the output stays within 16 bits, at both limits.
    std::vector<std::int16_t> input;
    for (int i = 0; i < 4000; ++i) {
        const std::int16_t value = (i / 7) % 2 == 0 ? std::int16_t{32767} : std::int16_t{-32768};
        input.push_back(value);
        input.push_back(value);
    }
    Mixer mixer(opl_rate, 44100);
    mixer.set_volume(2.0);
    const auto output = run(mixer, input, 199);
    SCI_CHECK(std::ranges::find(output, std::int16_t{32767}) != output.end());
    SCI_CHECK(std::ranges::find(output, std::int16_t{-32768}) != output.end());
}

SCI_TEST(mixer_speed_changes_the_output_length) {
    const std::vector<std::int16_t> input(static_cast<std::size_t>(opl_rate) * 2, 0);
    Mixer exact(opl_rate, 48000);
    Mixer faster(opl_rate, 48000);
    faster.set_speed(1.01);
    const auto normal = run(exact, input, 199);
    const auto fewer = run(faster, input, 199);
    // One second: about 1 % fewer frames when consuming 1 % faster.
    const auto expected = static_cast<std::int64_t>(48000.0 / 1.01);
    SCI_CHECK(std::abs(static_cast<std::int64_t>(normal.size() / 2) - 48000) <= 3);
    SCI_CHECK(std::abs(static_cast<std::int64_t>(fewer.size() / 2) - expected) <= 3);
}

SCI_TEST(queue_speed_follows_the_queue) {
    using sci::audio::queue_speed;
    SCI_CHECK(queue_speed(1000, 1000) == 1.0);
    SCI_CHECK(queue_speed(1500, 1000) > 1.0 && queue_speed(1500, 1000) < 1.002);
    SCI_CHECK(queue_speed(500, 1000) < 1.0 && queue_speed(500, 1000) > 0.998);
    // Bounded at 0.2 % either way.
    SCI_CHECK(std::abs(queue_speed(100000, 1000) - 1.002) < 1e-12);
    SCI_CHECK(std::abs(queue_speed(0, 1000) - 0.998) < 1e-12);
}
