// SPDX-License-Identifier: GPL-3.0-or-later
//
// The MT-32 synthesiser (sci::audio::Mt32 on mt32emu). The ROMs are the
// user's and never in the repository: the tests that need them read them from
// the folder named by SIERRA_HD_MT32_ROMS and pass, with a note, when it is
// not set or a file is missing (as in CI). The folder holds the files under
// the names below (src/sci-audio/README.md).

#include "sci/audio/mt32.hpp"
#include "sci/core/error.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using sci::audio::Mt32;
using sci::audio::Mt32Model;
using sci::core::ErrorCode;

namespace {

constexpr const char* roms_variable = "SIERRA_HD_MT32_ROMS";

using Rom = std::optional<std::vector<std::uint8_t>>;

// The ROM folder, or nothing (and a note) when it is not given.
std::optional<std::filesystem::path> rom_folder() {
    const char* folder = std::getenv(roms_variable); // NOLINT(concurrency-mt-unsafe): no threads here
    if (folder == nullptr || *folder == '\0') {
        std::println("  skipped: {} is not set", roms_variable);
        return std::nullopt;
    }
    return std::filesystem::path(folder);
}

// The first of `names` that exists in `folder`, read whole; nothing (and a
// note) when none does.
Rom read_rom(const std::filesystem::path& folder, std::initializer_list<std::string_view> names) {
    for (const std::string_view name : names) {
        std::ifstream file(folder / name, std::ios::binary);
        if (file) {
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
    }
    std::println("  skipped: {} is not in {}", *names.begin(), folder.string());
    return std::nullopt;
}

Rom mt32_control(const std::filesystem::path& folder) {
    return read_rom(folder, {"MT32_CONTROL.1987-10-07.v1.07.ROM", "MT32_CONTROL.ROM"});
}

Rom mt32_pcm(const std::filesystem::path& folder) {
    return read_rom(folder, {"MT32_PCM.ROM"});
}

Rom cm32l_control(const std::filesystem::path& folder) {
    return read_rom(folder, {"CM32L_CONTROL.1989-12-05.v1.02.ROM", "CM32L_CONTROL.ROM"});
}

Rom cm32l_pcm(const std::filesystem::path& folder) {
    return read_rom(folder, {"CM32L_PCM.ROM"});
}

// An MT-32 on the 1.07 control ROM and its PCM ROM, or nothing without ROMs.
std::unique_ptr<Mt32> open_mt32() {
    const auto folder = rom_folder();
    if (!folder) {
        return nullptr;
    }
    const auto control = mt32_control(*folder);
    const auto pcm = mt32_pcm(*folder);
    if (!control || !pcm) {
        return nullptr;
    }
    auto synth = Mt32::open(*control, *pcm);
    SCI_CHECK(synth.has_value());
    return synth ? std::move(*synth) : nullptr;
}

std::vector<std::int16_t> render(Mt32& synth, std::size_t frames) {
    std::vector<std::int16_t> samples(frames * 2);
    synth.render(samples);
    return samples;
}

int peak(std::span<const std::int16_t> samples) {
    int result = 0;
    for (const std::int16_t sample : samples) {
        result = std::max(result, sample < 0 ? -int{sample} : int{sample});
    }
    return result;
}

// A Roland DT1 (data set) SysEx to the MT-32 (device 0x10, model 0x16):
// `data` at the 3-byte address, with the checksum.
std::vector<std::uint8_t> data_set(std::array<std::uint8_t, 3> address, std::span<const std::uint8_t> data) {
    std::vector<std::uint8_t> sysex = {0xf0, 0x41, 0x10, 0x16, 0x12};
    int sum = 0;
    for (const std::uint8_t byte : address) {
        sysex.push_back(byte);
        sum += byte;
    }
    for (const std::uint8_t byte : data) {
        sysex.push_back(byte);
        sum += byte;
    }
    sysex.push_back(static_cast<std::uint8_t>((128 - (sum % 128)) % 128));
    sysex.push_back(0xf7);
    return sysex;
}

// Note on, key 60, velocity 100 on MIDI channel 2: part 1 of an MT-32 after
// power-on (piano).
constexpr std::uint32_t note_on = 0x91U | (60U << 8U) | (100U << 16U);
constexpr std::uint32_t note_off = 0x81U | (60U << 8U);

} // namespace

SCI_TEST(mt32_rejects_what_is_not_a_rom) {
    const std::vector<std::uint8_t> nothing;
    const auto empty = Mt32::open(nothing, nothing);
    SCI_CHECK(!empty && empty.error().code == ErrorCode::InvalidData);

    const std::vector<std::uint8_t> zeros(65536);
    const auto unknown = Mt32::open(zeros, std::vector<std::uint8_t>(524288));
    SCI_CHECK(!unknown && unknown.error().code == ErrorCode::InvalidData);
}

SCI_TEST(mt32_identifies_the_mt32_roms) {
    const auto synth = open_mt32();
    if (!synth) {
        return;
    }
    SCI_CHECK(synth->roms().model == Mt32Model::Mt32);
    SCI_CHECK(synth->roms().control_name == "ctrl_mt32_1_07");
    SCI_CHECK(synth->roms().control_description == "MT-32 Control v1.07");
    SCI_CHECK(synth->roms().pcm_name == "pcm_mt32");
    std::println("  {} + {}", synth->roms().control_description, synth->roms().pcm_description);
}

SCI_TEST(mt32_identifies_the_cm32l_roms) {
    const auto folder = rom_folder();
    if (!folder) {
        return;
    }
    const auto control = cm32l_control(*folder);
    const auto pcm = cm32l_pcm(*folder);
    if (!control || !pcm) {
        return;
    }
    const auto synth = Mt32::open(*control, *pcm);
    SCI_CHECK(synth.has_value());
    if (!synth) {
        return;
    }
    SCI_CHECK((*synth)->roms().model == Mt32Model::Cm32l);
    SCI_CHECK((*synth)->roms().control_name == "ctrl_cm32l_1_02");
    SCI_CHECK((*synth)->roms().pcm_name == "pcm_cm32l");
    std::println("  {} + {}", (*synth)->roms().control_description, (*synth)->roms().pcm_description);

    // A CM-32L plays part 1 too.
    (*synth)->play_now(note_on);
    SCI_CHECK(peak(render(**synth, 8000)) > 1000);
}

SCI_TEST(mt32_rejects_wrong_or_mismatched_roms) {
    const auto folder = rom_folder();
    if (!folder) {
        return;
    }
    const auto control = mt32_control(*folder);
    const auto pcm = mt32_pcm(*folder);
    const auto cm32l = cm32l_pcm(*folder);
    if (!control || !pcm || !cm32l) {
        return;
    }
    // Swapped: each file is a ROM, but not the kind expected in its place.
    const auto swapped = Mt32::open(*pcm, *control);
    SCI_CHECK(!swapped && swapped.error().code == ErrorCode::InvalidData);
    // The MT-32's control ROM expects the MT-32's 512 KB PCM ROM.
    const auto mismatched = Mt32::open(*control, *cm32l);
    SCI_CHECK(!mismatched && mismatched.error().code == ErrorCode::Unsupported);
    // A truncated control ROM is no known ROM.
    const auto truncated = Mt32::open(std::span(*control).first(control->size() - 1), *pcm);
    SCI_CHECK(!truncated && truncated.error().code == ErrorCode::InvalidData);
}

SCI_TEST(mt32_is_silent_when_idle_and_plays_a_note) {
    const auto synth = open_mt32();
    if (!synth) {
        return;
    }
    SCI_CHECK(peak(render(*synth, Mt32::native_rate)) == 0);
    SCI_CHECK(synth->rendered_frames() == Mt32::native_rate);

    synth->play_now(note_on);
    const auto note = render(*synth, 16000);
    SCI_CHECK(peak(note) > 1000);
    // Both sides carry it (part 1 is panned to the centre).
    int left = 0;
    int right = 0;
    for (std::size_t i = 0; i < note.size(); i += 2) {
        left = std::max(left, std::abs(int{note[i]}));
        right = std::max(right, std::abs(int{note[i + 1]}));
    }
    SCI_CHECK(left > 500 && right > 500);

    // Released, the note decays to a floor of a few steps that stays (2
    // steps 5 s after the note off; docs/ASSUMPTIONS.md, "Milestone 7:
    // mt32emu").
    synth->play_now(note_off);
    static_cast<void>(render(*synth, std::size_t{3} * Mt32::native_rate));
    SCI_CHECK(peak(render(*synth, 1000)) <= 4);
}

SCI_TEST(mt32_plays_timestamped_messages_at_their_frame) {
    const auto synth = open_mt32();
    if (!synth) {
        return;
    }
    static_cast<void>(render(*synth, 100));
    SCI_CHECK(synth->play_at(note_on, 100 + 8000));
    const auto samples = render(*synth, 16000);
    // Silent up to the timestamp (plus the MIDI cable's 1 ms for 3 bytes),
    // sounding after it.
    SCI_CHECK(peak(std::span(samples).first(std::size_t{2} * 8000)) == 0);
    SCI_CHECK(peak(std::span(samples).subspan(std::size_t{2} * 8100)) > 1000);
}

SCI_TEST(mt32_accepts_sysex) {
    const auto synth = open_mt32();
    if (!synth) {
        return;
    }
    // Display text (system area 0x20 00 00).
    constexpr std::string_view text = "sierra-hd";
    const std::vector<std::uint8_t> characters(text.begin(), text.end());
    synth->play_sysex_now(data_set({0x20, 0x00, 0x00}, characters));
    SCI_CHECK(synth->display_message() == text);

    // Master volume (system area 0x10 00 16) to 0: the note is almost
    // silent (mt32emu's volume 0 attenuates by about 44 dB here, not to nothing).
    synth->play_sysex_now(data_set({0x10, 0x00, 0x16}, std::array<std::uint8_t, 1>{0}));
    synth->play_now(note_on);
    SCI_CHECK(peak(render(*synth, 8000)) < 100);

    // Back to full volume, through the queue: the next note is loud again.
    // (The note is released and left to decay first: struck again while it
    // still sounds, the key keeps the quiet note's level.)
    synth->play_now(note_off);
    static_cast<void>(render(*synth, Mt32::native_rate));
    SCI_CHECK(synth->play_sysex_at(data_set({0x10, 0x00, 0x16}, std::array<std::uint8_t, 1>{100}),
                                   synth->rendered_frames() + 100));
    static_cast<void>(render(*synth, 200));
    synth->play_now(note_on);
    SCI_CHECK(peak(render(*synth, 8000)) > 1000);

    // A queued display text shows once rendering reaches it.
    SCI_CHECK(synth->play_sysex_at(data_set({0x20, 0x00, 0x00}, std::array<std::uint8_t, 2>{'o', 'k'}),
                                   synth->rendered_frames() + 50));
    SCI_CHECK(synth->display_message() == text);
    static_cast<void>(render(*synth, 100));
    SCI_CHECK(synth->display_message() == "ok");

    // A bad checksum is ignored, as on the device; an empty message too.
    auto bad = data_set({0x20, 0x00, 0x00}, std::array<std::uint8_t, 1>{'x'});
    bad[bad.size() - 2] ^= 0x01U;
    synth->play_sysex_now(bad);
    SCI_CHECK(synth->display_message() == "ok");
    synth->play_sysex_now({});
}

// mt32emu draws a pitch jitter from the C library's rand() (TVP.cpp, the
// timing of the MCU's pitch timer), so its output is a function of the ROMs,
// the input and rand()'s state; with the same seed it is the same.
SCI_TEST(mt32_output_is_the_same_on_every_run) {
    std::vector<std::vector<std::int16_t>> runs;
    for (int run = 0; run < 3; ++run) {
        const auto synth = open_mt32();
        if (!synth) {
            return;
        }
        if (run < 2) {
            // The state mt32emu reads, not a random source of ours.
            // NOLINTBEGIN(bugprone-unsafe-functions,bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
            std::srand(1);
            // NOLINTEND(bugprone-unsafe-functions,bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp)
        }
        // A chord on part 1, a drum on the rhythm part (channel 10), a
        // program change on part 2, all through the queue and at once.
        synth->play_now(note_on);
        synth->play_now(0x91U | (64U << 8U) | (90U << 16U));
        SCI_CHECK(synth->play_at(0x99U | (36U << 8U) | (127U << 16U), 4000));
        SCI_CHECK(synth->play_at(0xc2U | (48U << 8U), 6000));
        SCI_CHECK(synth->play_at(0x92U | (67U << 8U) | (110U << 16U), 6500));
        SCI_CHECK(synth->play_at(note_off, 20000));
        runs.push_back(render(*synth, std::size_t{2} * Mt32::native_rate));
    }
    SCI_CHECK(peak(runs[0]) > 1000);
    SCI_CHECK(runs[0] == runs[1]);
    // Not reseeded, the jitter differs.
    SCI_CHECK(runs[0] != runs[2]);
}
