// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 MT-32 driver (sci::audio::Mt32Driver, ScummVM's MidiPlayer_Midi):
// a synthetic patch.001, the SysEx messages sent when it opens, the routing
// of notes and programs, the volume and the parts of a song. Most tests run
// without ROMs (the driver then only logs what it sends); those that render
// read the ROMs from SIERRA_HD_MT32_ROMS and pass with a note without it.

#include "sci/audio/mt32.hpp"
#include "sci/audio/mt32_driver.hpp"
#include "sci/core/error.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using sci::audio::Mt32;
using sci::audio::Mt32Driver;
using sci::audio::Mt32Patch;
using sci::core::ErrorCode;

namespace {

constexpr std::size_t header_size = 491;

void put_text(std::vector<std::uint8_t>& data, std::size_t at, std::string_view text) {
    std::ranges::fill(std::span(data).subspan(at, Mt32Patch::text_size), ' ');
    std::ranges::copy(text, data.begin() + static_cast<std::ptrdiff_t>(at));
}

// A patch.001 with `timbres` timbres (each filled with its number), and
// optionally patches 49-96 (filled with 0x49) and the rhythm part (key map
// 0x52, partial reserve 1-9).
std::vector<std::uint8_t> synthetic_patch(std::size_t timbres, bool patches_49_96, bool rhythm) {
    std::vector<std::uint8_t> data(header_size, 0);
    put_text(data, 0, "After");
    put_text(data, 20, "Before");
    put_text(data, 40, "Goodbye");
    data[60] = 120; // Volume 120, cut to 100.
    data[62] = 2;   // Default reverb 2.
    // Reverb settings by column: mode i, time 10 + i, level 20 + i.
    for (std::size_t i = 0; i < Mt32Patch::reverb_configs; ++i) {
        data[74 + i] = static_cast<std::uint8_t>(i);
        data[74 + 11 + i] = static_cast<std::uint8_t>(10 + i);
        data[74 + 22 + i] = static_cast<std::uint8_t>(20 + i);
    }
    std::fill(data.begin() + 107, data.begin() + 491, 0x11);
    data.push_back(static_cast<std::uint8_t>(timbres));
    for (std::size_t t = 0; t < timbres; ++t) {
        data.insert(data.end(), Mt32Patch::timbre_size, static_cast<std::uint8_t>(t));
    }
    if (patches_49_96) {
        data.insert(data.end(), {0xAB, 0xCD});
        data.insert(data.end(), 384, 0x49);
    }
    if (rhythm) {
        data.insert(data.end(), {0xDC, 0xBA});
        data.insert(data.end(), 256, 0x52);
        for (std::uint8_t i = 1; i <= 9; ++i) {
            data.push_back(i);
        }
    }
    return data;
}

std::uint32_t address_of(const std::vector<std::uint8_t>& sysex) {
    return (std::uint32_t{sysex[0]} << 16U) | (std::uint32_t{sysex[1]} << 8U) | sysex[2];
}

std::vector<std::uint32_t> addresses(const Mt32Driver::Log& log) {
    std::vector<std::uint32_t> result;
    result.reserve(log.sysex.size());
    for (const auto& sysex : log.sysex) {
        result.push_back(address_of(sysex));
    }
    return result;
}

constexpr std::uint32_t midi(std::uint32_t status, std::uint32_t first, std::uint32_t second) {
    return status | (first << 8U) | (second << 16U);
}

std::optional<Mt32Patch> parsed(const std::vector<std::uint8_t>& data) {
    auto patch = sci::audio::parse_mt32_patch(data);
    SCI_CHECK(patch.has_value());
    return patch ? std::optional<Mt32Patch>(std::move(*patch)) : std::nullopt;
}

// An MT-32 on the 1.07 control ROM, or nothing (and a note) without ROMs.
std::unique_ptr<Mt32> open_mt32() {
    const char* folder = std::getenv("SIERRA_HD_MT32_ROMS"); // NOLINT(concurrency-mt-unsafe): no threads here
    if (folder == nullptr || *folder == '\0') {
        std::println("  skipped: SIERRA_HD_MT32_ROMS is not set");
        return nullptr;
    }
    const auto read = [&](std::initializer_list<std::string_view> names) -> std::vector<std::uint8_t> {
        for (const std::string_view name : names) {
            std::ifstream file(std::filesystem::path(folder) / name, std::ios::binary);
            if (file) {
                return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            }
        }
        return {};
    };
    const auto control = read({"MT32_CONTROL.1987-10-07.v1.07.ROM", "MT32_CONTROL.ROM"});
    const auto pcm = read({"MT32_PCM.ROM"});
    if (control.empty() || pcm.empty()) {
        std::println("  skipped: no MT-32 ROMs in {}", folder);
        return nullptr;
    }
    auto synth = Mt32::open(control, pcm);
    SCI_CHECK(synth.has_value());
    return synth ? std::move(*synth) : nullptr;
}

int peak_of(Mt32& synth, std::size_t frames) {
    std::vector<std::int16_t> samples(frames * 2);
    synth.render(samples);
    int peak = 0;
    for (const std::int16_t sample : samples) {
        peak = std::max(peak, sample < 0 ? -int{sample} : int{sample});
    }
    return peak;
}

} // namespace

SCI_TEST(mt32_patch_parses_all_parts) {
    const auto patch = parsed(synthetic_patch(2, true, true));
    if (!patch) {
        return;
    }
    SCI_CHECK(
        std::string_view(reinterpret_cast<const char*>(patch->after_text.data()), 5) == // NOLINT(*-reinterpret-cast)
        "After");
    SCI_CHECK(patch->before_text[0] == 'B' && patch->goodbye_text[0] == 'G');
    SCI_CHECK(patch->volume == 100);
    SCI_CHECK(patch->default_reverb == 2);
    SCI_CHECK((patch->reverb[3] == std::array<std::uint8_t, 3>{3, 13, 23}));
    SCI_CHECK(patch->timbres.size() == 2 && patch->timbres[1][245] == 1);
    SCI_CHECK(patch->patches_49_96.has_value() && (*patch->patches_49_96)[383] == 0x49);
    SCI_CHECK(patch->rhythm_key_map.has_value() && patch->partial_reserve[8] == 9);

    const auto plain = parsed(synthetic_patch(0, false, false));
    SCI_CHECK(plain && plain->timbres.empty() && !plain->patches_49_96 && !plain->rhythm_key_map);
}

SCI_TEST(mt32_patch_refuses_other_formats) {
    // Larger than any MT-32 patch: the General MIDI format.
    const auto gm = sci::audio::parse_mt32_patch(std::vector<std::uint8_t>(17000));
    SCI_CHECK(!gm && gm.error().code == ErrorCode::Unsupported);
    // Shorter than its timbres.
    std::vector<std::uint8_t> short_patch = synthetic_patch(2, false, false);
    short_patch.resize(short_patch.size() - 1);
    const auto truncated = sci::audio::parse_mt32_patch(short_patch);
    SCI_CHECK(!truncated && truncated.error().code == ErrorCode::InvalidData);
    // Long enough to be checked, but neither format.
    std::vector<std::uint8_t> junk = synthetic_patch(4, false, false);
    junk.push_back(0);
    const auto neither = sci::audio::parse_mt32_patch(junk);
    SCI_CHECK(!neither && neither.error().code == ErrorCode::InvalidData);
}

SCI_TEST(mt32_driver_sends_the_patch_when_it_opens) {
    const auto patch = parsed(synthetic_patch(2, true, true));
    Mt32Driver::Log log;
    {
        const Mt32Driver driver(nullptr, patch, &log);
        // MidiPlayer_Midi::open: the reset, the texts, volume, patches,
        // timbres, patches 49-96, rhythm key map, partial reserve, the
        // second text and the "mystery" SysEx.
        SCI_CHECK((addresses(log) == std::vector<std::uint32_t>{0x7F0000, 0x200000, 0x100016, 0x050000, 0x050200,
                                                                0x080000, 0x080200, 0x050300, 0x050500, 0x030110,
                                                                0x100004, 0x200000, 0x52000A}));
        SCI_CHECK(log.sysex[1][3] == 'B' && log.sysex[11][3] == 'A'); // Before, then after.
        SCI_CHECK((log.sysex[2] == std::vector<std::uint8_t>{0x10, 0x00, 0x16, 100}));
        SCI_CHECK(log.sysex[5].size() == 3 + Mt32Patch::timbre_size && log.sysex[6][3] == 1);
        SCI_CHECK(log.sysex[10].size() == 3 + 9);
        SCI_CHECK(log.midi.empty());
    }
    // Closing shows the goodbye text.
    SCI_CHECK(address_of(log.sysex.back()) == 0x200000 && log.sysex.back()[3] == 'G');

    // Without patch.001 only the reset is sent, and nothing at the close.
    Mt32Driver::Log bare;
    {
        const Mt32Driver driver(nullptr, std::nullopt, &bare);
    }
    SCI_CHECK((addresses(bare) == std::vector<std::uint32_t>{0x7F0000}));
}

SCI_TEST(mt32_driver_shows_each_message_as_sent) {
    std::vector<std::vector<std::uint8_t>> sent;
    Mt32Driver::Log log;
    log.on_send = [&](std::span<const std::uint8_t> message) { sent.emplace_back(message.begin(), message.end()); };
    Mt32Driver driver(nullptr, std::nullopt, &log);
    // The reset as sendMt32SysEx builds it: header, address, data, checksum.
    SCI_CHECK(
        (sent.at(0) == std::vector<std::uint8_t>{0x41, 0x10, 0x16, 0x12, 0x7F, 0x00, 0x00, 0x01, 0x00, 0x00}));
    sent.clear();
    driver.send(0x05C1); // Program 5 on channel 1: the volume, then the program (2 bytes), then the pan.
    driver.send(0x7F3C91);
    driver.send(0x2000E1);
    SCI_CHECK(sent.size() == 5);
    SCI_CHECK((sent.at(0) == std::vector<std::uint8_t>{0xB1, 0x07, 0x7F}));
    SCI_CHECK((sent.at(1) == std::vector<std::uint8_t>{0xC1, 0x05}));
    SCI_CHECK((sent.at(2) == std::vector<std::uint8_t>{0xB1, 0x0A, 0x40}));
    SCI_CHECK((sent.at(3) == std::vector<std::uint8_t>{0x91, 0x3C, 0x7F}));
    SCI_CHECK((sent.at(4) == std::vector<std::uint8_t>{0xE1, 0x00, 0x20}));
    // A song's SysEx as the player passes it on.
    sent.clear();
    const std::array<std::uint8_t, 7> song{0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x01};
    driver.sysex(song);
    SCI_CHECK(sent.size() == 1 && std::ranges::equal(sent.at(0), song));
}

SCI_TEST(mt32_driver_reports_its_player_choices) {
    const Mt32Driver driver(nullptr, std::nullopt);
    const sci::audio::PlayerChoices choices = driver.player_choices();
    SCI_CHECK(choices.play_id == 0x01 && choices.polyphony == 32 && choices.has_rhythm_channel);
}

SCI_TEST(mt32_driver_routes_notes_and_programs) {
    Mt32Driver::Log log;
    Mt32Driver driver(nullptr, std::nullopt, &log);
    log.sysex.clear();
    // A channel without a program drops its notes; the rhythm channel 9
    // plays without one.
    driver.send(midi(0x91, 60, 100));
    driver.send(midi(0x99, 36, 90));
    SCI_CHECK((log.midi == std::vector<std::uint32_t>{midi(0x99, 36, 90)}));
    log.midi.clear();
    // The first program sets the channel's volume (127 at master 15), then
    // the program and the pan; a repeated program sends nothing.
    driver.send(midi(0xC1, 5, 0));
    driver.send(midi(0xC1, 5, 0));
    SCI_CHECK((log.midi == std::vector<std::uint32_t>{midi(0xB1, 7, 127), midi(0xC1, 5, 0), midi(0xB1, 0x0A, 0x40)}));
    log.midi.clear();
    // Notes now pass, a note off as a note on with velocity 0; a program
    // on the rhythm channel, aftertouch and voice mapping (0x4B) are not
    // sent; pitch bend passes unchanged.
    driver.send(midi(0x91, 60, 100));
    driver.send(midi(0x81, 60, 64));
    driver.send(midi(0xC9, 3, 0));
    driver.send(midi(0xA1, 60, 10));
    driver.send(midi(0xD1, 10, 0));
    driver.send(midi(0xB1, 0x4B, 2));
    driver.send(midi(0xE1, 0, 0x50));
    SCI_CHECK((log.midi == std::vector<std::uint32_t>{midi(0x91, 60, 100), midi(0x91, 60, 0), midi(0xE1, 0, 0x50)}));
    SCI_CHECK(log.sysex.empty());
}

SCI_TEST(mt32_driver_scales_the_volume) {
    Mt32Driver::Log log;
    Mt32Driver driver(nullptr, std::nullopt, &log);
    driver.send(midi(0xB2, 7, 100));
    SCI_CHECK((log.midi == std::vector<std::uint32_t>{midi(0xB2, 7, 100)}));
    log.midi.clear();
    // The master volume scales channels 1-9: 100 * 6 / 15 = 40, others
    // (127 by default) 50.
    driver.set_volume(6);
    SCI_CHECK(log.midi.size() == 9 && log.midi[0] == midi(0xB1, 7, 50) && log.midi[1] == midi(0xB2, 7, 40));
    log.midi.clear();
    // A small volume stays audible: 1 * 6 / 15 rounds up to 1; 0 is 0.
    driver.send(midi(0xB3, 7, 1));
    driver.send(midi(0xB3, 7, 0));
    SCI_CHECK((log.midi == std::vector<std::uint32_t>{midi(0xB3, 7, 1), midi(0xB3, 7, 0)}));
    log.midi.clear();
    // Sound off silences channels 1-9 and holds back volume changes; on
    // again restores them.
    driver.play_switch(false);
    SCI_CHECK(log.midi.size() == 9 && log.midi[8] == midi(0xB9, 7, 0));
    log.midi.clear();
    driver.send(midi(0xB2, 7, 30));
    SCI_CHECK(log.midi.empty());
    driver.play_switch(true);
    SCI_CHECK(log.midi.size() == 9 && log.midi[1] == midi(0xB2, 7, 12));
}

SCI_TEST(mt32_driver_assigns_the_parts_of_a_song) {
    const auto patch = parsed(synthetic_patch(0, false, false));
    Mt32Driver::Log log;
    Mt32Driver driver(nullptr, patch, &log);
    log.sysex.clear();
    // Channels 1, 2 and 8 have the MT-32 bit; channel 9 the rhythm flag.
    std::vector<std::uint8_t> header(0x21, 0);
    header[2 + 2] = 0x01;
    header[2 + 4] = 0x05;
    header[2 + 16] = 0x01;
    header[2 + 6] = 0x04; // Channel 3: the AdLib only.
    header[19] = 0x80;
    driver.init_track(header);
    // The patch's default reverb (2) first, then the parts.
    SCI_CHECK((addresses(log) == std::vector<std::uint32_t>{0x100001, 0x10000D}));
    SCI_CHECK((log.sysex[0] == std::vector<std::uint8_t>{0x10, 0x00, 0x01, 2, 12, 22}));
    SCI_CHECK((log.sysex[1] == std::vector<std::uint8_t>{0x10, 0x00, 0x0D, 1, 2, 0x10, 0x10, 0x10, 0x10, 0x10, 8, 9}));
    log.sysex.clear();
    // The same reverb again is not sent; another one is; -1 (none) is only
    // stored.
    driver.init_track(header);
    SCI_CHECK((addresses(log) == std::vector<std::uint32_t>{0x10000D}));
    log.sysex.clear();
    driver.set_reverb(4);
    driver.set_reverb(-1);
    SCI_CHECK((addresses(log) == std::vector<std::uint32_t>{0x100001}));
    // A header with other capabilities assigns nothing.
    log.sysex.clear();
    header[0] = 1;
    driver.init_track(header);
    SCI_CHECK((addresses(log) == std::vector<std::uint32_t>{0x100001}));
}

SCI_TEST(mt32_driver_passes_song_sysex_on) {
    Mt32Driver::Log log;
    Mt32Driver driver(nullptr, std::nullopt, &log);
    log.sysex.clear();
    // A Roland DT1 without its 0xF0 and 0xF7: written without the header and
    // the checksum. Other commands are ignored.
    const std::array<std::uint8_t, 9> dt1{0x41, 0x10, 0x16, 0x12, 0x10, 0x00, 0x16, 0x50, 0x0A};
    driver.sysex(dt1);
    const std::array<std::uint8_t, 6> other{0x41, 0x10, 0x16, 0x11, 0x00, 0x00};
    driver.sysex(other);
    SCI_CHECK((log.sysex == std::vector<std::vector<std::uint8_t>>{{0x10, 0x00, 0x16, 0x50}}));
}

SCI_TEST(mt32_driver_plays_on_the_synthesiser) {
    auto synth = open_mt32();
    if (!synth) {
        return;
    }
    std::vector<std::uint8_t> data = synthetic_patch(0, false, false);
    // Patches 1-48: all on the preset timbre group 0, timbre 0 (piano).
    std::fill(data.begin() + 107, data.begin() + 491, 0);
    data[62] = 0xFF; // No default reverb.
    for (std::size_t p = 0; p < 48; ++p) {
        data[107 + (p * 8) + 2] = 24; // Key shift 0.
        data[107 + (p * 8) + 3] = 50; // Fine tune 0.
        data[107 + (p * 8) + 4] = 12; // Bender range 12.
        data[107 + (p * 8) + 6] = 1;  // Reverb on.
    }
    const auto patch = parsed(data);
    Mt32Driver driver(std::move(synth), patch);
    // The second text stays on the display.
    SCI_CHECK(driver.synth().display_message().starts_with("After"));
    std::vector<std::uint8_t> header(0x21, 0);
    header[2 + 2] = 0x01; // Channel 1 on part 1.
    driver.init_track(header);
    SCI_CHECK(peak_of(driver.synth(), 3200) == 0);
    // Without a program the note is dropped.
    driver.send(midi(0x91, 60, 100));
    SCI_CHECK(peak_of(driver.synth(), 8000) == 0);
    driver.send(midi(0x81, 60, 0));
    driver.send(midi(0xC1, 0, 0));
    driver.send(midi(0x91, 60, 100));
    const int loud = peak_of(driver.synth(), 8000);
    SCI_CHECK(loud > 1000);
    // A lower master volume plays it more quietly.
    driver.send(midi(0x81, 60, 0));
    peak_of(driver.synth(), 64000);
    driver.set_volume(4);
    driver.send(midi(0x91, 60, 100));
    const int quiet = peak_of(driver.synth(), 8000);
    SCI_CHECK(quiet > 0 && quiet < loud / 2);
    std::println("  peak {} at master volume 15, {} at 4", loud, quiet);
}
