// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 music player with a device attached: what the device receives
// and when (ScummVM's MidiParser_SCI and SciMusic), that the player's
// timing, signals and cues do not depend on the device, and rendering a
// synthetic song through the AdLib driver (sci::vm::render_sound).

#include "faults.hpp"
#include "music.hpp"
#include "sci/audio/adlib.hpp"
#include "sci/audio/midi_device.hpp"
#include "sci/vm/sound_render.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using sci::vm::Faults;
using sci::vm::Music;
using sci::vm::MusicEntry;
using sci::vm::SoundResource;

namespace {

// An SCI0 sound whose channels in `adlib_channels` have one AdLib voice
// each (voice count, then the play mask 4, per channel in the header).
std::vector<std::uint8_t> sci0_sound(std::initializer_list<int> adlib_channels,
                                     std::initializer_list<std::uint8_t> song) {
    std::vector<std::uint8_t> data(0x21, 0);
    for (const int channel : adlib_channels) {
        data[1 + (static_cast<std::size_t>(channel) * 2)] = 1;
        data[2 + (static_cast<std::size_t>(channel) * 2)] = 0x04;
    }
    data.insert(data.end(), song);
    return data;
}

// Program 0 and a note at tick 0, the note off and a signal 5 at tick 30, a
// cue at tick 60, the end at tick 90.
const std::vector<std::uint8_t> one_note_song =
    sci0_sound({0}, {
                        0x00, 0xC0, 0x00, 0x00, 0x90, 0x3C, 0x7F, 0x1E, 0x80, 0x3C,
                        0x00, 0x00, 0xCF, 0x05, 0x1E, 0xBF, 0x60, 0x02, 0x1E, 0xFC,
                    });

// Records what reaches the device, as text.
class RecordingDevice final : public sci::audio::MidiDevice {
public:
    std::vector<std::string> calls;

    void send(std::uint32_t message) override { calls.push_back(std::format("send {:06x}", message)); }

    void init_track(std::span<const std::uint8_t> header) override {
        calls.push_back(std::format("init {}", header.size()));
    }

    void set_volume(std::uint8_t volume) override { calls.push_back(std::format("volume {}", volume)); }

    void play_switch(bool play) override { calls.push_back(std::format("switch {}", play)); }

    void on_timer() override { calls.emplace_back("timer"); }
};

struct Player {
    Faults faults;
    Music music{faults};

    MusicEntry& add(const std::vector<std::uint8_t>& sound, std::uint16_t loop) {
        auto entry = std::make_unique<MusicEntry>();
        entry->sound_object = sci::vm::make_reg(1, 1);
        entry->resource_id = 1;
        entry->loop = loop;
        entry->resource = SoundResource::parse(sound, 1);
        music.sound_init(*entry);
        return music.add(std::move(entry));
    }
};

// An audible instrument 0: a sine carrier with the fastest envelope.
std::vector<sci::audio::AdlibPatch> audible_patches() {
    std::vector<std::uint8_t> bank(1344, 0);
    bank[1] = 1;       // Modulator multiplier 1.
    bank[8] = 63;      // Modulator total level 63 (silent).
    bank[3] = 15;      // Modulator attack.
    bank[13 + 1] = 1;  // Carrier multiplier 1.
    bank[13 + 3] = 15; // Carrier attack.
    bank[13 + 7] = 15; // Carrier release.
    bank[13 + 4] = 0;  // Sustain level 0 (loudest).
    bank[13 + 5] = 1;  // Sustained while the key is down.
    bank[12] = 1;      // FM.
    auto patches = sci::audio::parse_adlib_patches(bank);
    SCI_CHECK(patches.has_value());
    return patches ? std::move(*patches) : std::vector<sci::audio::AdlibPatch>(96);
}

} // namespace

SCI_TEST(kdosound_sends_wait_for_the_next_timer_call) {
    Player player;
    RecordingDevice device;
    player.music.set_device(&device);
    MusicEntry& sound = player.add(one_note_song, 1);
    device.calls.clear();
    player.music.sound_play(sound);
    // The volume is set at once (127 * master 15 / 127); the init commands,
    // the track init and the note-offs of jumpToTick(0) wait in the queue.
    SCI_CHECK((device.calls == std::vector<std::string>{"volume 15"}));
    device.calls.clear();
    player.music.on_timer();
    // Channel 15 counts as used: it carries the signal and the cue.
    std::vector<std::string> expected{
        "send 7f07b0", "send 400ab0", "send 0040b0", "send 4000e0", "send 7f07bf",
        "send 400abf", "send 0040bf", "send 4000ef", "init 33",
    };
    for (int channel = 0; channel < 16; ++channel) {
        expected.push_back(std::format("send 007b{:02x}", 0xB0 + channel));
        expected.push_back(std::format("send 0040{:02x}", 0xB0 + channel));
    }
    // Then tick 0 of the song plays on the timer, and the driver's own step.
    expected.insert(expected.end(), {"send 0000c0", "send 7f3c90", "timer"});
    SCI_CHECK(device.calls == expected);
    // Stopping sends the note-offs of the notes playing: none after the
    // note off at tick 30.
    for (int i = 0; i < 130; ++i) {
        player.music.on_timer();
    }
    device.calls.clear();
    player.music.sound_stop(sound);
    player.music.on_timer();
    SCI_CHECK(device.calls.size() == 33 && device.calls.back() == "timer");
    SCI_CHECK(std::ranges::count(device.calls, std::string("send 003c80")) == 0);
    SCI_CHECK(!player.faults.raised());
}

SCI_TEST(stopping_a_song_releases_its_playing_notes) {
    Player player;
    RecordingDevice device;
    player.music.set_device(&device);
    MusicEntry& sound = player.add(one_note_song, 1);
    player.music.sound_play(sound);
    player.music.on_timer(); // The note starts.
    device.calls.clear();
    player.music.sound_stop(sound);
    player.music.on_timer();
    SCI_CHECK(device.calls.size() == 34 && device.calls.front() == "send 003c80");
    // Sound off reaches the device at once.
    device.calls.clear();
    player.music.set_sound_on(false);
    SCI_CHECK((device.calls == std::vector<std::string>{"switch false"}));
}

SCI_TEST(signals_cues_and_ticks_do_not_depend_on_the_device) {
    Player silent;
    Player heard;
    const auto driver = std::make_unique<sci::audio::AdlibDriver>(audible_patches());
    heard.music.set_device(driver.get());
    MusicEntry& a = silent.add(one_note_song, 2);
    MusicEntry& b = heard.add(one_note_song, 2);
    silent.music.sound_play(a);
    heard.music.sound_play(b);
    for (int i = 0; i < 1000; ++i) {
        silent.music.on_timer();
        heard.music.on_timer();
        SCI_CHECK(std::tie(a.signal, a.data_inc, a.ticker, a.loop, a.signal_queue, a.status) ==
                  std::tie(b.signal, b.data_inc, b.ticker, b.loop, b.signal_queue, b.status));
        a.signal = 0;
        b.signal = 0;
    }
    heard.music.clear_play_list();
    heard.music.set_device(nullptr);
}

SCI_TEST(a_synthetic_song_renders_until_its_end) {
    const auto render = [] {
        sci::audio::AdlibDriver driver(audible_patches());
        return sci::vm::render_sound(
            one_note_song, 1, driver,
            {.max_frames = 10 * sci::audio::Opl3::native_rate, .loop = 1, .master_volume = 15});
    };
    const auto first = render();
    SCI_CHECK(first.has_value());
    if (!first) {
        return;
    }
    SCI_CHECK(first->ended);
    // The end at tick 90 plays on timer call 376 (375 * 4000 < 90 * 16667
    // <= 376 * 4000); the frames up to the next call follow it.
    SCI_CHECK(first->timer_calls == 376);
    SCI_CHECK(first->samples.size() == 2 * (376ULL * sci::audio::Opl3::native_rate / 250));
    // The note sounds in its 30 ticks (125 calls) and is quiet after its
    // release.
    const auto peak = [&](std::size_t from, std::size_t to) {
        int value = 0;
        for (std::size_t i = from; i < to; ++i) {
            value = std::max(value, std::abs(static_cast<int>(first->samples[i])));
        }
        return value;
    };
    const std::size_t call = 2 * sci::audio::Opl3::native_rate / 250;
    SCI_CHECK(peak(0, 120 * call) > 1000);
    SCI_CHECK(peak(200 * call, first->samples.size()) <= 1);
    // Deterministic.
    const auto second = render();
    SCI_CHECK(second.has_value() && second->samples == first->samples);
    // Too short for an SCI0 header.
    sci::audio::AdlibDriver driver(audible_patches());
    SCI_CHECK(!sci::vm::render_sound(std::vector<std::uint8_t>(10), 1, driver, {.max_frames = 100}).has_value());
}

SCI_TEST(the_opl_log_of_a_render_is_keyed_by_the_timer_call) {
    std::vector<std::tuple<std::uint32_t, std::uint16_t, std::uint8_t>> writes;
    sci::audio::AdlibDriver driver(audible_patches());
    sci::vm::SoundRenderOptions options{.max_frames = 10 * sci::audio::Opl3::native_rate, .loop = 1, .master_volume = 15};
    options.on_opl = [&](std::uint32_t call, std::uint16_t reg, std::uint8_t value) {
        writes.emplace_back(call, reg, value);
    };
    const auto rendered = sci::vm::render_sound(one_note_song, 1, driver, options);
    SCI_CHECK(rendered.has_value());
    SCI_CHECK(!writes.empty());
    SCI_CHECK(std::ranges::is_sorted(writes, {}, [](const auto& write) { return std::get<0>(write); }));
    // The note's key-on on the left chip in the first timer call (the
    // queued commands go out then), its key-off 30 ticks (125 calls) later.
    const auto key = [&](bool on) {
        return std::ranges::find_if(writes, [&](const auto& write) {
            return std::get<1>(write) == 0xB0 && ((std::get<2>(write) & 0x20U) != 0) == on;
        });
    };
    SCI_CHECK(key(true) != writes.end() && std::get<0>(*key(true)) == 1);
    SCI_CHECK(key(false) != writes.end() && std::get<0>(*key(false)) == 126);
    // The hook is gone after the render.
    writes.clear();
    driver.send(0x90 | (60U << 8U) | (127U << 16U));
    SCI_CHECK(writes.empty());
}
