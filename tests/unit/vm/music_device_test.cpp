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

    void sysex(std::span<const std::uint8_t> message) override {
        calls.push_back(std::format("sysex {}", message.size()));
    }

    void set_reverb(std::int8_t reverb) override { calls.push_back(std::format("reverb {}", reverb)); }

    [[nodiscard]] sci::audio::PlayerChoices player_choices() const override { return choices; }

    sci::audio::PlayerChoices choices = sci::vm::silent_choices;
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
    sci::vm::SoundRenderOptions options{
        .max_frames = 10 * sci::audio::Opl3::native_rate,
        .loop = 1,
        .master_volume = 15,
    };
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
    driver.send(0x90U | (60U << 8U) | (127U << 16U));
    SCI_CHECK(writes.empty());
}

namespace {

// Channel 1 for the MT-32 only (play mask 0x01), channel 2 for the AdLib
// only (0x04), channel 9 (rhythm) for neither; a note on each at tick 0, a
// signal 7 at tick 10 and a cue at tick 20 on the control channel, the end
// at tick 40.
std::vector<std::uint8_t> two_device_song() {
    std::vector<std::uint8_t> data(0x21, 0);
    data[2 + 2] = 0x01;
    data[2 + 4] = 0x04;
    data.insert(data.end(), {
                                0x00, 0xC1, 0x05, 0x00, 0x91, 0x40, 0x60, 0x00, 0xC2, 0x06, 0x00, 0x92, 0x41, 0x60,
                                0x00, 0x99, 0x24, 0x60, 0x0A, 0xCF, 0x07, 0x0A, 0xBF, 0x60, 0x01, 0x14, 0xFC,
                            });
    return data;
}

bool sent(const RecordingDevice& device, const std::string& call) {
    return std::ranges::count(device.calls, call) > 0;
}

} // namespace

SCI_TEST(the_player_asks_the_device_for_its_choices) {
    // Without a device (the silent driver) and with the AdLib: its play
    // mask, nine voices and no rhythm channel.
    const Player silent;
    SCI_CHECK(silent.music.voices() == 9);
    SCI_CHECK(silent.music.player_choices() == sci::vm::silent_choices);

    Player adlib;
    RecordingDevice adlib_device;
    adlib.music.set_device(&adlib_device);
    MusicEntry& on_adlib = adlib.add(two_device_song(), 1);
    adlib.music.sound_play(on_adlib);
    adlib.music.on_timer();
    SCI_CHECK(sent(adlib_device, "send 604192") && !sent(adlib_device, "send 604091") &&
              !sent(adlib_device, "send 602499"));

    // The MT-32's choices: its play mask 0x01, 32 voices and the rhythm
    // channel 9, which plays whatever its play mask.
    Player mt32;
    RecordingDevice mt32_device;
    mt32_device.choices = {.play_id = 0x01, .polyphony = 32, .has_rhythm_channel = true};
    mt32.music.set_device(&mt32_device);
    SCI_CHECK(mt32.music.voices() == 32);
    MusicEntry& on_mt32 = mt32.add(two_device_song(), 1);
    mt32.music.sound_play(on_mt32);
    mt32.music.on_timer();
    SCI_CHECK(sent(mt32_device, "send 604091") && !sent(mt32_device, "send 604192") &&
              sent(mt32_device, "send 602499"));

    // The signals, cues, ticks and the end are the same on every device.
    Player plain;
    MusicEntry& on_none = plain.add(two_device_song(), 1);
    plain.music.sound_play(on_none);
    plain.music.on_timer();
    for (int i = 0; i < 400; ++i) {
        plain.music.on_timer();
        adlib.music.on_timer();
        mt32.music.on_timer();
        SCI_CHECK(std::tie(on_none.signal, on_none.data_inc, on_none.ticker, on_none.status) ==
                  std::tie(on_mt32.signal, on_mt32.data_inc, on_mt32.ticker, on_mt32.status));
        SCI_CHECK(std::tie(on_adlib.signal, on_adlib.data_inc, on_adlib.ticker, on_adlib.status) ==
                  std::tie(on_mt32.signal, on_mt32.data_inc, on_mt32.ticker, on_mt32.status));
        on_none.signal = 0;
        on_adlib.signal = 0;
        on_mt32.signal = 0;
    }
    SCI_CHECK(on_mt32.data_inc == 1 && !on_mt32.parser->is_tracking());
    adlib.music.clear_play_list();
    adlib.music.set_device(nullptr);
    mt32.music.clear_play_list();
    mt32.music.set_device(nullptr);
}

SCI_TEST(the_player_passes_reverb_and_sysex_to_the_device) {
    Player player;
    RecordingDevice device;
    player.music.set_device(&device);
    // A reverb controller (0x50) and a SysEx (its length, then the data, as
    // ScummVM's parser reads it) on the control channel.
    std::vector<std::uint8_t> data(0x21, 0);
    data.insert(data.end(), {0x00, 0xBF, 0x50, 0x03, 0x00, 0xF0, 0x03, 0x41, 0x10, 0xF7, 0x0A, 0xFC});
    MusicEntry& sound = player.add(data, 1);
    player.music.sound_play(sound);
    device.calls.clear();
    player.music.on_timer();
    // The trailing 0xF7 is dropped.
    SCI_CHECK(sent(device, "reverb 3") && sent(device, "sysex 2"));
    player.music.clear_play_list();
    player.music.set_device(nullptr);
}

SCI_TEST(a_track_init_sets_the_driver_reverb_to_the_device_default) {
    // MidiPlayer_Midi::initTrack sets the driver's reverb to the patch's
    // default before every SCI0 song: getReverb, which a save records,
    // reports it.
    Player player;
    RecordingDevice device;
    device.choices.default_reverb = 6;
    player.music.set_device(&device);
    player.music.set_driver_reverb(2);
    MusicEntry& sound = player.add(one_note_song, 1);
    player.music.sound_play(sound);
    SCI_CHECK(player.music.driver_reverb() == 2); // The init waits for the timer.
    player.music.on_timer();
    SCI_CHECK(player.music.driver_reverb() == 6);
    // Without a default (the AdLib), the driver's reverb stays.
    device.choices.default_reverb = -1;
    player.music.set_driver_reverb(2);
    player.music.sound_play(sound);
    player.music.on_timer();
    SCI_CHECK(player.music.driver_reverb() == 2);
    player.music.clear_play_list();
    player.music.set_device(nullptr);
}

SCI_TEST(the_global_reverb_reaches_the_song_playing_if_it_has_none) {
    // SciMusic::setGlobalReverb: another value than 127 is the global
    // reverb, which the first song playing takes if its own is 127 (none);
    // 127 sets the reverb of that song.
    Player player;
    RecordingDevice device;
    player.music.set_device(&device);
    player.music.set_global_reverb(4);
    SCI_CHECK(player.music.global_reverb() == 4 && device.calls.empty()); // Nothing plays.
    MusicEntry& sound = player.add(one_note_song, 1);
    player.music.sound_play(sound);
    SCI_CHECK(sound.reverb == 127);
    device.calls.clear();
    player.music.set_global_reverb(2);
    SCI_CHECK(player.music.global_reverb() == 2 && player.music.driver_reverb() == 2);
    SCI_CHECK((device.calls == std::vector<std::string>{"reverb 2"}));
    sound.reverb = 5;
    device.calls.clear();
    player.music.set_global_reverb(3);
    SCI_CHECK(player.music.global_reverb() == 3 && device.calls.empty());
    player.music.set_global_reverb(127);
    SCI_CHECK(player.music.global_reverb() == 3 && player.music.driver_reverb() == 5);
    SCI_CHECK((device.calls == std::vector<std::string>{"reverb 5"}));
    player.music.clear_play_list();
    player.music.set_device(nullptr);
}
