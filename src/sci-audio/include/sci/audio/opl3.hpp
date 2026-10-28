// SPDX-License-Identifier: GPL-3.0-or-later
//
// One OPL3 chip (YMF262), emulated cycle-accurately by Nuked OPL3 (spec
// section 8; third_party/nuked-opl3). An AdLib card's OPL2 (YM3812) is the
// chip's compatibility mode, which it starts in after a reset.
//
// This is the deterministic core: integer-only, no threads, no OS, all
// state in the object. Its output is a pure function of the sequence of
// writes and generate() calls, the same on every platform, which is what the
// OPL golden hashes check. Samples are produced at the chip's native rate
// (OPL3 master clock 14.31818 MHz / 288 = 49716 Hz); resampling to the
// output device's rate is the mixer's job and outside the hashes.

#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace sci::audio {

class Opl3 {
public:
    // Samples per second of generate() output.
    static constexpr std::uint32_t native_rate = 49716;
    // Registers 0x000-0x0ff are the first bank (the OPL2 registers), 0x100-0x1ff
    // the second (OPL3 only).
    static constexpr std::uint16_t register_count = 0x200;

    // A chip in its power-on state, as after reset().
    Opl3();
    ~Opl3();
    Opl3(const Opl3&) = delete;
    Opl3& operator=(const Opl3&) = delete;
    Opl3(Opl3&&) = delete;
    Opl3& operator=(Opl3&&) = delete;

    // Back to the power-on state: all registers zero, all envelopes released,
    // no writes pending, OPL2 compatibility mode.
    void reset() noexcept;

    // Queues a register write with the bus timing of the real chip
    // (Nuked's buffered writes): a write takes effect at the next generated
    // sample, but at least 2 samples (about 40 us) after the previous one, so
    // a burst of writes is spread out as a driver's wait loops would. This is
    // the write a driver uses; `reg` is below register_count.
    void write(std::uint16_t reg, std::uint8_t value) noexcept;

    // Applies a register write at once, with no bus delay.
    void write_now(std::uint16_t reg, std::uint8_t value) noexcept;

    // Generates stereo sample frames, interleaved left and right, into
    // `frames` (whose size is even), one frame per chip sample at
    // native_rate. In OPL2 mode both sides carry every channel; some channels
    // reach the left side one sample later than the right (a chip quirk Nuked
    // reproduces).
    void generate(std::span<std::int16_t> frames) noexcept;

private:
    struct Chip;
    // On the heap: the chip state is about 17 KB and points into itself.
    std::unique_ptr<Chip> chip_;
};

} // namespace sci::audio
