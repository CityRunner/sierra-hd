// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the SCI0 music player (sci-vm) sends to a music device: the part of
// ScummVM's MidiPlayer interface (engines/sci/sound/drivers/mididriver.h)
// that the SCI0 player calls. The silent driver is no device at all: the
// player then drops these calls, and its timing, signals and cues are the
// same with or without a device (docs/ASSUMPTIONS.md, "Milestone 7: the
// AdLib driver").
//
// Calls are deterministic and come from the one thread that runs the player.

#pragma once

#include <cstdint>
#include <span>

namespace sci::audio {

class MidiDevice {
public:
    MidiDevice() = default;
    virtual ~MidiDevice() = default;
    MidiDevice(const MidiDevice&) = delete;
    MidiDevice& operator=(const MidiDevice&) = delete;
    MidiDevice(MidiDevice&&) = delete;
    MidiDevice& operator=(MidiDevice&&) = delete;

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

    // The device's own work after each call of the music timer, which in
    // ScummVM the device's timer drives (MidiDriver_AdLib::onTimer).
    virtual void on_timer() = 0;
};

} // namespace sci::audio
