// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Roland MT-32 (or CM-32L), emulated by mt32emu from the Munt project
// (spec section 8; third_party/mt32emu). The control and PCM ROMs are the
// user's (never bundled); they are passed in as bytes, since sci-audio does
// no file I/O, and the caller (app) reads them from the configured folder.
//
// No threads, no OS: MIDI goes in and stereo samples come out on the calling
// thread. Samples are produced at the synthesiser's native rate of 32000 Hz
// (mt32emu's coarse analogue output mode, as ScummVM uses it); resampling to
// the output device's rate is the mixer's job. mt32emu's integer renderer is
// used: for a given ROM pair and the same sequence of calls the output is the
// same on every run (tested), but unlike the OPL core it is not hashed and
// need not match across platforms (docs/ASSUMPTIONS.md, "Milestone 7:
// mt32emu").

#pragma once

#include "sci/core/error.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace sci::audio {

// The synthesiser a control ROM belongs to. The CM-32L (and LAPC-I) has the
// MT-32's sounds plus 33 sound effects on part 10's keys, and a few
// differences in its reverb and in its handling of some messages; mt32emu
// emulates each from its ROM.
enum class Mt32Model : std::uint8_t {
    Mt32,
    Cm32l,
};

// What mt32emu identified the ROMs as, from their size and SHA-1.
struct Mt32Roms {
    Mt32Model model;
    // mt32emu's short names and descriptions, e.g. "ctrl_mt32_1_07" and
    // "MT-32 Control v1.07", "pcm_mt32" and "MT-32 PCM ROM".
    std::string control_name;
    std::string control_description;
    std::string pcm_name;
    std::string pcm_description;
};

class Mt32 {
public:
    // Stereo frames per second of render() output.
    static constexpr std::uint32_t native_rate = 32000;

    // Opens a synthesiser on a control ROM and a PCM ROM, each a whole ROM
    // image as dumped (not a pair of halves). The bytes are copied. Fails
    // with InvalidData when a file is not a ROM mt32emu knows (or not the
    // kind of ROM expected in its place), with Unsupported when the two
    // ROMs do not belong to the same synthesiser, or when mt32emu cannot
    // open the synthesiser on them.
    [[nodiscard]] static core::Result<std::unique_ptr<Mt32>> open(std::span<const std::uint8_t> control_rom,
                                                                  std::span<const std::uint8_t> pcm_rom);

    ~Mt32();
    Mt32(const Mt32&) = delete;
    Mt32& operator=(const Mt32&) = delete;
    Mt32(Mt32&&) = delete;
    Mt32& operator=(Mt32&&) = delete;

    [[nodiscard]] const Mt32Roms& roms() const noexcept;

    // Frames rendered since open(): the clock of the timestamps below.
    [[nodiscard]] std::uint32_t rendered_frames() const noexcept;

    // Plays a MIDI channel message at once, before the next rendered frame:
    // the status byte in bits 0-7, the first data byte in bits 8-15, the
    // second in bits 16-23 (as MidiDevice::send). A message that is not a
    // channel message is ignored.
    void play_now(std::uint32_t message) noexcept;

    // Plays a System Exclusive message at once, framed (F0 ... F7). The
    // synthesiser checks the Roland header and checksum itself and ignores
    // a message it does not accept, as the real one does.
    void play_sysex_now(std::span<const std::uint8_t> sysex) noexcept;

    // Queues a channel message or a SysEx to play when rendering reaches the
    // frame `timestamp` (on the rendered_frames() clock; a timestamp in the
    // past plays at the next rendered frame). Short messages are delayed
    // further by the MIDI cable's transfer time, as on the real device.
    // Returns false when the queue (1024 events) is full.
    [[nodiscard]] bool play_at(std::uint32_t message, std::uint32_t timestamp) noexcept;
    [[nodiscard]] bool play_sysex_at(std::span<const std::uint8_t> sysex, std::uint32_t timestamp) noexcept;

    // Renders stereo frames, interleaved left and right, into `frames`
    // (whose size is even), at native_rate.
    void render(std::span<std::int16_t> frames) noexcept;

    // The last text shown on the synthesiser's display by a SysEx (empty if
    // none since open()); for logs and tests.
    [[nodiscard]] const std::string& display_message() const noexcept;

private:
    struct State;
    explicit Mt32(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace sci::audio
