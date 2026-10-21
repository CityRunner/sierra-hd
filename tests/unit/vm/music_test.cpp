// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 music player on synthetic songs. The expected timer counts follow
// ScummVM's MidiParser: an event at song tick t is played by the first timer
// call n (every 4000 µs) with n * 4000 >= t * 16667, counting from the last
// event played.

#include "faults.hpp"
#include "music.hpp"
#include "sci_test.hpp"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

using sci::vm::Faults;
using sci::vm::Music;
using sci::vm::MusicEntry;
using sci::vm::signal_offset;
using sci::vm::SoundResource;
using sci::vm::SoundStatus;

namespace {

// An SCI0 sound: the 0x21-byte header with the AdLib play mask (4) on the
// channels in `adlib_channels`, then the song.
std::vector<std::uint8_t> sci0_sound(std::initializer_list<int> adlib_channels,
                                     std::initializer_list<std::uint8_t> song) {
    std::vector<std::uint8_t> data(0x21, 0);
    for (const int channel : adlib_channels) {
        data[2 + (static_cast<std::size_t>(channel) * 2)] = 0x04;
    }
    data.insert(data.end(), song);
    return data;
}

// Note on, a signal 5 at tick 30, a cue of 2 at tick 60, then the end.
const std::vector<std::uint8_t> signal_and_cue_song = sci0_sound({0}, {
                                                                          0x00,
                                                                          0x90,
                                                                          0x3C,
                                                                          0x40, // Tick 0: note on, channel 0.
                                                                          0x1E,
                                                                          0xCF,
                                                                          0x05, // Tick 30: signal 5.
                                                                          0x1E,
                                                                          0xBF,
                                                                          0x60,
                                                                          0x02, // Tick 60: cue +2.
                                                                          0x00,
                                                                          0xFC, // End of track.
                                                                      });

struct Player {
    Faults faults;
    Music music{faults};
    SoundResource resource;
    std::vector<std::uint8_t> data;

    MusicEntry& add(const std::vector<std::uint8_t>& sound, std::uint16_t loop, std::int16_t priority, int id) {
        auto entry = std::make_unique<MusicEntry>();
        entry->sound_object = sci::vm::make_reg(1, id);
        entry->resource_id = static_cast<std::uint16_t>(id);
        entry->loop = loop;
        entry->priority = priority;
        entry->resource = SoundResource::parse(sound, id);
        music.sound_init(*entry);
        return music.add(std::move(entry));
    }

    void run(int calls) {
        for (int i = 0; i < calls; ++i) {
            music.on_timer();
        }
    }
};

} // namespace

SCI_TEST(signals_and_cues_arrive_on_the_timer_call_scummvm_plays_them) {
    Player player;
    MusicEntry& sound = player.add(signal_and_cue_song, 1, 0, 1);
    player.music.sound_play(sound);
    SCI_CHECK(sound.status == SoundStatus::Playing);
    player.run(125);
    SCI_CHECK(sound.signal == 0); // 125 * 4000 < 30 * 16667.
    player.run(1);
    SCI_CHECK(sound.signal == 5);
    player.run(124);
    SCI_CHECK(sound.data_inc == 0);
    player.run(1); // Call 251: 251 * 4000 >= 60 * 16667.
    SCI_CHECK(sound.data_inc == 2);
    // The end of the song comes while signal 5 is still pending: queued.
    SCI_CHECK(sound.signal == 5);
    SCI_CHECK((sound.signal_queue == std::vector<std::uint16_t>{signal_offset}));
    SCI_CHECK(sound.loop == 0);
    sound.signal = 0; // As kGetEvent's cue update does.
    player.run(1);
    SCI_CHECK(sound.signal == signal_offset && sound.signal_queue.empty());
    SCI_CHECK(!player.faults.raised());
}

SCI_TEST(looping_songs_restart_from_the_loop_point) {
    Player player;
    MusicEntry& sound = player.add(signal_and_cue_song, 2, 0, 1);
    player.music.sound_play(sound);
    player.run(126);
    SCI_CHECK(sound.signal == 5);
    sound.signal = 0;
    player.run(125); // Call 251 reaches the end and jumps back to tick 0.
    SCI_CHECK(sound.loop == 1 && sound.signal == 0 && sound.signal_queue.empty());
    SCI_CHECK(sound.data_inc == 2);
    // The rest of call 251 is lost: the song restarts at call 252.
    player.run(125);
    SCI_CHECK(sound.signal == 0);
    player.run(1);
    SCI_CHECK(sound.signal == 5);
    player.run(125);
    SCI_CHECK(sound.data_inc == 4 && sound.loop == 0);
    SCI_CHECK((sound.signal_queue == std::vector<std::uint16_t>{signal_offset}));
}

SCI_TEST(songs_without_loops_do_not_advance) {
    // MusicEntry::onTimer skips sounds whose loop count is 0.
    Player player;
    MusicEntry& sound = player.add(signal_and_cue_song, 0, 0, 1);
    player.music.sound_play(sound);
    player.run(300);
    SCI_CHECK(sound.signal == 0 && sound.data_inc == 0 && sound.ticker == 0);
}

SCI_TEST(channels_of_other_devices_are_filtered_but_keep_their_time) {
    // Channel 1 is not played by AdLib: its note goes, its delta stays.
    const std::vector<std::uint8_t> song = sci0_sound({0}, {
                                                               0x0A,
                                                               0x91,
                                                               0x3C,
                                                               0x40, // Tick 10: note on, channel 1 (filtered).
                                                               0x0A,
                                                               0xCF,
                                                               0x07, // Tick 20: signal 7.
                                                               0x00,
                                                               0xFC,
                                                           });
    Player player;
    MusicEntry& sound = player.add(song, 1, 0, 1);
    SCI_CHECK(sound.resource && sound.resource->channel_filter_mask(sci::vm::adlib_play_id, false) == 0x8001U);
    player.music.sound_play(sound);
    player.run(83); // 83 * 4000 < 20 * 16667 <= 84 * 4000.
    SCI_CHECK(sound.signal == 0);
    player.run(1);
    SCI_CHECK(sound.signal == 7);
}

SCI_TEST(tickers_count_song_ticks) {
    Player player;
    MusicEntry& sound = player.add(signal_and_cue_song, 1, 0, 1);
    player.music.sound_play(sound);
    player.run(150);
    // (150 * 4000 - 30 * 16667) / 16667 + 30 = 35.
    SCI_CHECK(sound.ticker == 35);
}

SCI_TEST(lower_priority_songs_wait_and_resume_when_the_first_stops) {
    Player player;
    MusicEntry& first = player.add(signal_and_cue_song, 1, 5, 1);
    MusicEntry& second = player.add(signal_and_cue_song, 1, 2, 2);
    player.music.sound_play(first);
    player.music.sound_play(second);
    SCI_CHECK(first.status == SoundStatus::Playing && second.status == SoundStatus::Paused);
    // The playlist is in priority order.
    SCI_CHECK(&player.music.at(0) == &first && &player.music.at(1) == &second);
    player.music.sound_stop(first);
    SCI_CHECK(first.status == SoundStatus::Stopped && second.status == SoundStatus::Playing);
}

SCI_TEST(higher_priority_songs_pause_the_one_playing) {
    Player player;
    MusicEntry& first = player.add(signal_and_cue_song, 1, 2, 1);
    MusicEntry& second = player.add(signal_and_cue_song, 1, 5, 2);
    player.music.sound_play(first);
    player.run(10);
    player.music.sound_play(second);
    SCI_CHECK(first.status == SoundStatus::Paused && second.status == SoundStatus::Playing);
    SCI_CHECK(first.pause_counter == 1);
    // On resuming, the first song fast-forwards to where it was.
    player.music.sound_stop(second);
    SCI_CHECK(first.status == SoundStatus::Playing && first.pause_counter == 0);
    SCI_CHECK(first.parser->tick() == first.ticker);
}

SCI_TEST(digital_samples_stop_the_machine) {
    std::vector<std::uint8_t> sound = sci0_sound({0}, {0x00, 0xFC, 0x01, 0x02});
    sound[0] = 2;
    Player player;
    const MusicEntry& entry = player.add(sound, 1, 0, 1);
    SCI_CHECK(entry.resource && entry.resource->has_digital_sample && player.faults.raised());
}
