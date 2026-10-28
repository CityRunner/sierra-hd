// SPDX-License-Identifier: GPL-3.0-or-later
//
// The software mixer (spec section 8): takes the music device's stereo
// output at its own rate (the OPL chip's 49716 Hz) and makes the output
// device's (whatever the audio layer gives, such as 48000 or 44100 Hz),
// with a volume and clipping to 16 bits.
//
// Resampling and output are outside the golden hashes (spec section 8), so
// this part may use floating point (double, spec section 5.3) and need not
// be bit-exact across platforms; the samples it is given stay the
// deterministic ones. It owns no thread and does no I/O: the caller feeds
// it and passes its output on (docs/ASSUMPTIONS.md, "Milestone 7: mixer
// and audio output").
//
// Resampling is 4-point cubic Hermite (Catmull-Rom) interpolation: four
// multiply-adds per sample, a much flatter passband than linear
// interpolation (which already loses about 3 dB at a third of the input
// rate) and lower images, while the OPL's output above the output
// device's Nyquist frequency is small enough that a long windowed-sinc
// filter would not be audibly better.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sci::audio {

class Mixer {
public:
    // Interleaved stereo frames in, at `input_rate`, and out, at
    // `output_rate` (both above zero).
    Mixer(std::uint32_t input_rate, std::uint32_t output_rate);

    // The gain applied to every sample: 0 is silent, 1 unchanged. Values
    // above 1 amplify (and may clip).
    void set_volume(double volume);

    // Fine correction of the output rate, for keeping the output's queue
    // filled as the two clocks drift (docs/ASSUMPTIONS.md): above 1 the
    // mixer consumes input faster, and so makes fewer output frames for the
    // same input (the pitch rises by the same small factor). 1 is exact.
    void set_speed(double speed);

    // Appends to `output` every output frame the input given so far allows,
    // after appending `input` (interleaved left and right, an even count).
    // Samples beyond the 16-bit range are clipped. Output lags the input by
    // two input frames (the interpolation's look-ahead).
    void process(std::span<const std::int16_t> input, std::vector<std::int16_t>& output);

private:
    // Input frames waiting, interleaved; frame 0 is the one before the
    // interpolation's current interval.
    std::vector<double> pending_;
    // The output position in input frames, from frame 1 of `pending_`.
    double position_ = 0.0;
    double step_;
    double base_step_;
    double volume_ = 1.0;
};

// The Mixer::set_speed that keeps an output queue near `target` frames
// when it holds `queued`: up to 0.2 % faster when it is too full, slower
// when it runs low, in proportion (about 3.5 cents at most, inaudible). The
// producer follows one clock and the output device another; this absorbs
// their drift (docs/ASSUMPTIONS.md, "Milestone 7: mixer and audio output").
[[nodiscard]] double queue_speed(std::size_t queued, std::size_t target);

} // namespace sci::audio
