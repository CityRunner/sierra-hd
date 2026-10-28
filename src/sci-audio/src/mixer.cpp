// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/audio/mixer.hpp"

#include "sci/core/assert.hpp"

#include <algorithm>
#include <cmath>

namespace sci::audio {

namespace {

constexpr std::size_t channels = 2;

// Catmull-Rom between `p0` (t = 0) and `p1` (t = 1).
double interpolate(double before, double p0, double p1, double after, double t) {
    const double linear = p1 - before;
    const double square = (2.0 * before) - (5.0 * p0) + (4.0 * p1) - after;
    const double cube = (3.0 * (p0 - p1)) + after - before;
    return p0 + (0.5 * t * (linear + (t * (square + (t * cube)))));
}

std::int16_t clip(double sample) {
    const double rounded = std::floor(sample + 0.5);
    return static_cast<std::int16_t>(std::clamp(rounded, -32768.0, 32767.0));
}

} // namespace

Mixer::Mixer(std::uint32_t input_rate, std::uint32_t output_rate)
    : pending_(channels, 0.0), // A silent frame before the first.
      step_(static_cast<double>(input_rate) / static_cast<double>(output_rate)), base_step_(step_) {
    SCI_ASSERT(input_rate > 0 && output_rate > 0);
}

void Mixer::set_volume(double volume) {
    volume_ = std::max(volume, 0.0);
}

void Mixer::set_speed(double speed) {
    SCI_ASSERT(speed > 0.0);
    step_ = base_step_ * speed;
}

void Mixer::process(std::span<const std::int16_t> input, std::vector<std::int16_t>& output) {
    SCI_ASSERT(input.size() % channels == 0);
    pending_.insert(pending_.end(), input.begin(), input.end());
    const std::size_t frames = pending_.size() / channels;
    // Frame 1 of pending_ is at position 0; the interval from frame i + 1
    // needs frames i to i + 3.
    while (true) {
        const double whole = std::floor(position_);
        const auto index = static_cast<std::size_t>(whole);
        if (index + 3 >= frames) {
            break;
        }
        const double t = position_ - whole;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const auto at = [&](std::size_t frame) { return pending_[(frame * channels) + channel]; };
            const double sample = interpolate(at(index), at(index + 1), at(index + 2), at(index + 3), t);
            output.push_back(clip(sample * volume_));
        }
        position_ += step_;
    }
    // Keep the frames the next output still needs.
    const auto consumed = std::min(static_cast<std::size_t>(std::floor(position_)), frames);
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(consumed * channels));
    position_ -= static_cast<double>(consumed);
}

double queue_speed(std::size_t queued, std::size_t target) {
    constexpr double max_correction = 0.002;
    if (target == 0) {
        return 1.0;
    }
    const double error = (static_cast<double>(queued) - static_cast<double>(target)) / static_cast<double>(target);
    return 1.0 + (max_correction * std::clamp(error, -1.0, 1.0));
}

} // namespace sci::audio
