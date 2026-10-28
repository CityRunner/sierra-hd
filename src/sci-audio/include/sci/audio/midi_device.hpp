// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the SCI0 music player (sci-vm) sends to a music device and asks of
// it: the part of ScummVM's MidiPlayer interface
// (engines/sci/sound/drivers/mididriver.h) that the SCI0 player calls. The
// silent driver is no device at all: the player then drops these calls and
// makes the AdLib driver's choices, so its timing, signals and cues are the
// same with the AdLib or without a device (docs/ASSUMPTIONS.md, "Milestone
// 7: the AdLib driver"). The MT-32 makes other choices ("Milestone 7: the
// MT-32 driver").
//
// Calls are deterministic and come from the one thread that runs the player.

#pragma once

#include <cstdint>
#include <span>

namespace sci::audio {

// What the player asks the device (MidiPlayer::getPlayId, getPolyphony and
// hasRhythmChannel): which channels of a song it plays and the voices
// kDoSound GetPolyphony reports. The timer rate is not asked: both devices
// run the music timer at 250 calls a second.
struct PlayerChoices {
    // The device's bit in the play masks of an SCI0 sound's channels.
    std::uint8_t play_id = 0;
    int polyphony = 0;
    // Channel 9 plays whatever its play mask says.
    bool has_rhythm_channel = false;

    friend bool operator==(const PlayerChoices&, const PlayerChoices&) = default;
};

class MidiDevice {
public:
    MidiDevice() = default;
    virtual ~MidiDevice() = default;
    MidiDevice(const MidiDevice&) = delete;
    MidiDevice& operator=(const MidiDevice&) = delete;
    MidiDevice(MidiDevice&&) = delete;
    MidiDevice& operator=(MidiDevice&&) = delete;

    [[nodiscard]] virtual PlayerChoices player_choices() const = 0;

    // One MIDI channel message: the status byte in bits 0-7, the first data
    // byte in bits 8-15, the second in bits 16-23 (MidiDriver::send).
    virtual void send(std::uint32_t message) = 0;

    // MidiPlayer::initTrack: the header of an SCI0 sound resource, before
    // the song plays.
    virtual void init_track(std::span<const std::uint8_t> header) = 0;

    // MidiPlayer::setVolume: the global volume, 0-15.
    virtual void set_volume(std::uint8_t volume) = 0;

    // MidiPlayer::playSwitch: kDoSound's sound on and off.
    virtual void play_switch(bool play) = 0;

    // MidiPlayer::sysExNoDelay: a SysEx message of a song, without its
    // 0xF0 and 0xF7 (MidiParser::processEvent). The default ignores it, as
    // the AdLib driver does; neither device asks for a delay after it.
    virtual void sysex(std::span<const std::uint8_t> /*message*/) {}

    // MidiPlayer::setReverb: a song's reverb, from its controller 0x50 (-1
    // for none). The default ignores it, as the AdLib driver does.
    virtual void set_reverb(std::int8_t /*reverb*/) {}

    // The device's own work after each call of the music timer, which in
    // ScummVM the device's timer drives (MidiDriver_AdLib::onTimer).
    virtual void on_timer() = 0;
};

} // namespace sci::audio
