// SPDX-License-Identifier: GPL-3.0-or-later
//
// The OPL core (sci::audio::Opl3 on Nuked OPL3). The golden hash is the
// SHA-256 of the generated samples, each written as two bytes little-endian,
// left then right; it is the same on every platform (spec section 8).

#include "sci/audio/opl3.hpp"
#include "sci/core/sha256.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using sci::audio::Opl3;

namespace {

struct Write {
    std::uint16_t reg;
    std::uint8_t value;
};

// One melodic note on channel 0 in OPL2 mode, as an AdLib driver sets it up:
// a two-operator FM voice, then key-on at F-number 0x241, block 4
// (577 * 49716 / 2^16 = 437.7 Hz).
constexpr std::array note_on = {
    Write{0x01, 0x20}, // waveform select enable (OPL2 test register)
    Write{0x20, 0x01}, // modulator: multiple 1
    Write{0x40, 0x10}, // modulator: total level 16 (-12 dB)
    Write{0x60, 0xf0}, // modulator: attack 15, decay 0
    Write{0x80, 0x77}, // modulator: sustain level 7, release 7
    Write{0x23, 0x01}, // carrier: multiple 1
    Write{0x43, 0x00}, // carrier: total level 0
    Write{0x63, 0xf0}, // carrier: attack 15, decay 0
    Write{0x83, 0x77}, // carrier: sustain level 7, release 7
    Write{0xc0, 0x08}, // channel 0: feedback 4, FM
    Write{0xa0, 0x41}, // channel 0: F-number low byte
    Write{0xb0, 0x32}, // channel 0: key on, block 4, F-number high bits 2
};
constexpr Write note_off{0xb0, 0x12};

// About 82 ms at the native rate.
constexpr std::size_t frame_count = 4096;

void write_all(Opl3& chip, std::span<const Write> writes) {
    for (const Write& write : writes) {
        chip.write(write.reg, write.value);
    }
}

std::vector<std::int16_t> generate(Opl3& chip, std::size_t frames) {
    std::vector<std::int16_t> samples(2 * frames);
    chip.generate(samples);
    return samples;
}

bool silent(std::span<const std::int16_t> samples) {
    return std::ranges::all_of(samples, [](std::int16_t sample) { return sample == 0; });
}

std::string hash_of(std::span<const std::int16_t> samples) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(2 * samples.size());
    for (const std::int16_t sample : samples) {
        const auto bits = static_cast<std::uint16_t>(sample);
        bytes.push_back(static_cast<std::uint8_t>(bits & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>(bits >> 8U));
    }
    return sci::core::to_hex(sci::core::sha256(bytes));
}

void append(std::vector<std::int16_t>& samples, const std::vector<std::int16_t>& part) {
    samples.insert(samples.end(), part.begin(), part.end());
}

// The golden sequence: the note for frame_count frames, then key-off and
// frame_count frames of its release.
std::vector<std::int16_t> play_note(Opl3& chip) {
    write_all(chip, note_on);
    std::vector<std::int16_t> samples = generate(chip, frame_count);
    chip.write(note_off.reg, note_off.value);
    append(samples, generate(chip, frame_count));
    return samples;
}

// The golden hash of play_note; a change means the core's output changed
// (a new Nuked OPL3 commit or a change in the wrapper).
constexpr std::string_view note_hash = "cd61c6656a46a40173510be0bf233a614a241cbfa94d4eb537995fd57325b076";

} // namespace

SCI_TEST(opl3_is_silent_after_reset) {
    Opl3 chip;
    SCI_CHECK(silent(generate(chip, frame_count)));
}

SCI_TEST(opl3_is_silent_with_a_voice_but_no_key_on) {
    Opl3 chip;
    write_all(chip, std::span(note_on).first(note_on.size() - 1));
    SCI_CHECK(silent(generate(chip, frame_count)));
}

SCI_TEST(opl3_key_on_makes_sound_on_both_sides) {
    Opl3 chip;
    const std::vector<std::int16_t> samples = play_note(chip);
    bool left = false;
    bool right = false;
    for (std::size_t i = 0; i < frame_count; ++i) {
        left = left || samples[2 * i] != 0;
        right = right || samples[(2 * i) + 1] != 0;
    }
    SCI_CHECK(left);
    SCI_CHECK(right);
}

SCI_TEST(opl3_buffered_writes_take_effect_two_samples_apart) {
    // Twelve queued writes, the first at sample 2: the key-on is applied at
    // sample 2 * 12 = 24, so the first 24 frames are silent, while an
    // immediate key-on sounds at once.
    constexpr std::size_t key_on_frame = 2 * note_on.size();
    Opl3 queued;
    write_all(queued, note_on);
    const std::vector<std::int16_t> late = generate(queued, frame_count);
    SCI_CHECK(silent(std::span(late).first(2 * key_on_frame)));
    SCI_CHECK(!silent(late));

    Opl3 direct;
    for (const Write& write : note_on) {
        direct.write_now(write.reg, write.value);
    }
    const std::vector<std::int16_t> early = generate(direct, frame_count);
    SCI_CHECK(!silent(std::span(early).first(2 * key_on_frame)));
}

SCI_TEST(opl3_note_has_the_golden_hash) {
    Opl3 chip;
    SCI_CHECK(hash_of(play_note(chip)) == note_hash);
}

SCI_TEST(opl3_note_is_released_to_the_floor) {
    Opl3 chip;
    const std::vector<std::int16_t> note = play_note(chip);
    SCI_CHECK(std::ranges::max(note) > 1000);
    // Release rate 7 takes well under a second from the sustain level. A
    // released operator whose phase still runs is not quite silent: at full
    // attenuation the negative half-wave gives -1, as on the real chip.
    static_cast<void>(generate(chip, Opl3::native_rate));
    const std::vector<std::int16_t> released = generate(chip, frame_count);
    SCI_CHECK(std::ranges::all_of(released, [](std::int16_t sample) { return sample == 0 || sample == -1; }));
    SCI_CHECK(!silent(released));
}

SCI_TEST(opl3_instances_are_independent) {
    Opl3 alone;
    const std::string expected = hash_of(play_note(alone));

    // Two chips used in turn, one playing and one not: the playing one gives
    // the same output as alone, the other stays silent.
    constexpr std::size_t block = 256;
    auto playing = std::make_unique<Opl3>();
    auto quiet = std::make_unique<Opl3>();
    std::vector<std::int16_t> samples;
    std::vector<std::int16_t> quiet_samples;
    write_all(*playing, note_on);
    quiet->write(0x01, 0x20);
    for (std::size_t frame = 0; frame < 2 * frame_count; frame += block) {
        if (frame == frame_count) {
            playing->write(note_off.reg, note_off.value);
        }
        append(samples, generate(*playing, block));
        append(quiet_samples, generate(*quiet, block));
    }
    SCI_CHECK(hash_of(samples) == expected);
    SCI_CHECK(silent(quiet_samples));
}

SCI_TEST(opl3_reset_restores_the_initial_state) {
    Opl3 chip;
    const std::string first = hash_of(play_note(chip));
    // Mid-note, with writes still queued and a second-bank register set.
    write_all(chip, note_on);
    chip.write_now(0x105, 0x01);
    static_cast<void>(generate(chip, 7));
    chip.reset();
    SCI_CHECK(silent(generate(chip, frame_count)));
    chip.reset();
    SCI_CHECK(hash_of(play_note(chip)) == first);
}

SCI_TEST(opl3_output_does_not_depend_on_the_block_size) {
    Opl3 whole;
    write_all(whole, note_on);
    const std::vector<std::int16_t> expected = generate(whole, frame_count);

    Opl3 pieces;
    write_all(pieces, note_on);
    std::vector<std::int16_t> samples;
    for (const std::size_t size : {std::size_t{1}, std::size_t{3}, std::size_t{1000}, frame_count - 1004}) {
        append(samples, generate(pieces, size));
    }
    SCI_CHECK(samples == expected);
}
