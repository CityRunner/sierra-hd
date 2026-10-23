// SPDX-License-Identifier: GPL-3.0-or-later
//
// The SCI0 AdLib driver. Ported from ScummVM (https://www.scummvm.org) at
// revision ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/sound/drivers/adlib.cpp (MidiDriver_AdLib and
//     MidiPlayer_AdLib, the SCI0-late paths),
//   audio/softsynth/opl/nuked.cpp (OPL::init, OPL::write and OPL::dualWrite)
// and translated into this project's style, types and error handling.
// Left out, as SCI1 or later only: the velocity tables and channel volumes
// of calcVelocity, extra voices and donateVoices, the SCI1.1 rhythm key map,
// findVoiceLateSci11 and its voice queue. The SCI0-early header of initTrack
// is not ported (the game's sound is SCI0 late).
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#include "sci/audio/adlib.hpp"

#include "sci/core/log.hpp"
#include "sci/resource/resource_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace sci::audio {

namespace {

constexpr std::array<std::uint8_t, AdlibDriver::voice_count> register_offset{
    0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12,
};

// One octave with three pitch wheel positions after each note.
constexpr std::array<int, 48> adlib_freq{
    0x157, 0x15c, 0x161, 0x166, 0x16b, 0x171, 0x176, 0x17b, 0x181, 0x186, 0x18c, 0x192, 0x198, 0x19e, 0x1a4, 0x1aa,
    0x1b0, 0x1b6, 0x1bd, 0x1c3, 0x1ca, 0x1d0, 0x1d7, 0x1de, 0x1e5, 0x1ec, 0x1f3, 0x1fa, 0x202, 0x209, 0x211, 0x218,
    0x220, 0x228, 0x230, 0x238, 0x241, 0x249, 0x252, 0x25a, 0x263, 0x26c, 0x275, 0x27e, 0x287, 0x290, 0x29a, 0x2a4,
};

constexpr std::size_t instrument_size = 28;
constexpr std::size_t bank_48_size = 1344;
constexpr std::size_t bank_96_size = 2690;
constexpr std::size_t bank_sci11_size = 5382;
constexpr int all_notes_off = 0x7B; // SCI_MIDI_CHANNEL_NOTES_OFF.

// MidiDriver_AdLib::loadInstrument.
AdlibPatch load_instrument(std::span<const std::uint8_t, instrument_size> ins) {
    AdlibPatch patch;
    for (std::size_t i = 0; i < 2; ++i) {
        const std::span<const std::uint8_t> op = ins.subspan(i * 13, 13);
        AdlibOperator& out = patch.op[i];
        out.kb_scale_level = op[0] & 0x3U;
        out.frequency_mult = op[1] & 0xFU;
        out.attack_rate = op[3] & 0xFU;
        out.sustain_level = op[4] & 0xFU;
        out.envelope_type = op[5] != 0;
        out.decay_rate = op[6] & 0xFU;
        out.release_rate = op[7] & 0xFU;
        out.total_level = op[8] & 0x3FU;
        out.amplitude_mod = op[9] != 0;
        out.vibrato = op[10] != 0;
        out.kb_scale_rate = op[11] != 0;
    }
    patch.op[0].wave_form = ins[26] & 0x3U;
    patch.op[1].wave_form = ins[27] & 0x3U;
    patch.feedback = ins[2] & 0x7U;
    patch.algorithm = ins[12] == 0; // The flag is inverted.
    return patch;
}

// Register numbers and values are composed unsigned; every operand here is
// non-negative.
constexpr unsigned bits(int value) {
    return static_cast<unsigned>(value);
}

void warn(std::string_view message) {
    core::log(core::LogLevel::Warning, core::LogCategory::General, message);
}

} // namespace

core::Result<std::vector<AdlibPatch>> parse_adlib_patches(std::span<const std::uint8_t> data) {
    const std::size_t size = data.size();
    if (size == bank_sci11_size) {
        return core::make_error(core::ErrorCode::Unsupported, "ADLIB: the SCI1.1 patch format is not SCI0");
    }
    if (size != bank_48_size && size != bank_96_size) {
        return core::make_error(core::ErrorCode::Unsupported,
                                std::format("ADLIB: Unsupported patch format ({} bytes)", size));
    }
    std::vector<AdlibPatch> patches;
    patches.reserve(96);
    const auto instrument = [&](std::size_t offset) {
        patches.push_back(load_instrument(data.subspan(offset).first<instrument_size>()));
    };
    for (std::size_t i = 0; i < 48; ++i) {
        instrument(instrument_size * i);
    }
    if (size == bank_48_size) {
        // Only 48 instruments: 48 dummies follow.
        constexpr std::array<std::uint8_t, instrument_size> dummy{};
        for (std::size_t i = 0; i < 48; ++i) {
            patches.push_back(load_instrument(dummy));
        }
    } else {
        for (std::size_t i = 48; i < 96; ++i) {
            instrument(2 + (instrument_size * i));
        }
    }
    return patches;
}

core::Result<std::vector<AdlibPatch>> load_adlib_patches(const resource::ResourceManager& resources) {
    const resource::ResourceId id{.type = resource::ResourceType::Patch, .number = 3};
    if (resources.contains(id)) {
        auto patch = resources.load(id);
        if (!patch) {
            return std::unexpected(std::move(patch.error()));
        }
        return parse_adlib_patches(patch->data);
    }
    // Early SCI0 games have the sound bank in the AdLib driver.
    if (const resource::GameFile* driver = resources.file("ADL.DRV")) {
        const std::size_t size = driver->bytes.size();
        constexpr std::size_t bank_offset = 0x45A;
        if (size == 5684 || size == 5720 || size == 5727) {
            return parse_adlib_patches(std::span(driver->bytes).subspan(bank_offset, bank_48_size));
        }
    }
    warn("ADLIB: Failed to load patch.003");
    return core::make_error(core::ErrorCode::NotFound, "Failed to initialize sound driver: no AdLib patches");
}

// AdlibDriver -------------------------------------------------------------------------

AdlibDriver::AdlibDriver(std::vector<AdlibPatch> patches) : patches_(std::move(patches)) {
    // OPL::init for a dual OPL2: OPL3 mode, written at once.
    opl_.write_now(0x105, 0x01);
    set_register(0xBD, 0);
    set_register(0x08, 0);
    set_register(0x01, 0x20);
}

void AdlibDriver::set_volume(std::uint8_t volume) {
    master_volume_ = volume;
    renew_notes(-1, true);
}

void AdlibDriver::send(std::uint32_t message) {
    const unsigned command = message & 0xF0U;
    const int channel = static_cast<int>(message & 0xFU);
    const unsigned first = (message >> 8U) & 0xFFU;
    const unsigned second = (message >> 16U) & 0xFFU;
    const int param1 = static_cast<int>(first);
    const int param2 = static_cast<int>(second);
    Channel& state = channels_[static_cast<std::size_t>(channel)];

    switch (command) {
    case 0x80:
        note_off(channel, param1);
        break;
    case 0x90:
        note_on(channel, param1, param2);
        break;
    case 0xB0:
        switch (param1) {
        case 0x07:
            state.volume = static_cast<std::uint8_t>(second >> 1U);
            renew_notes(channel, true);
            break;
        case 0x0A:
            state.pan = static_cast<std::uint8_t>(param2);
            renew_notes(channel, true);
            break;
        case 0x40:
            state.hold_pedal = static_cast<std::uint8_t>(param2);
            if (param2 == 0) {
                for (int i = 0; i < voice_count; ++i) {
                    const Voice& voice = voices_[static_cast<std::size_t>(i)];
                    if (voice.channel == channel && voice.is_sustained) {
                        voice_off(i);
                    }
                }
            }
            break;
        case 0x4B:
            voice_mapping(channel, param2);
            break;
        case 0x4E:
            state.enable_velocity = param2 != 0;
            break;
        case all_notes_off:
            for (int i = 0; i < voice_count; ++i) {
                const Voice& voice = voices_[static_cast<std::size_t>(i)];
                if (voice.channel == channel && voice.note != -1) {
                    voice_off(i);
                }
            }
            break;
        default:
            break;
        }
        break;
    case 0xC0:
        state.patch = static_cast<std::uint8_t>(param1);
        break;
    case 0xA0: // Aftertouch, which Sierra's driver ignores.
    case 0xD0:
        break;
    case 0xE0:
        state.pitch_wheel = static_cast<std::uint16_t>((first & 0x7FU) | ((second & 0x7FU) << 7U));
        renew_notes(channel, true);
        break;
    default:
        warn(std::format("ADLIB: Unknown event {:02x}", command));
        break;
    }
}

void AdlibDriver::init_track(std::span<const std::uint8_t> header) {
    // SCI0 late: the digital sample flag, then for each channel the voice
    // count and the play mask; channels the AdLib plays get their voices.
    std::size_t read = 0;
    const auto next = [&] { return read < header.size() ? header[read++] : std::uint8_t{0}; };
    const std::uint8_t caps = next();
    if (caps != 0 && caps != 2) {
        return;
    }
    for (Voice& voice : voices_) {
        voice.channel = -1;
        voice.mapped_channel = -1;
        voice.note = -1;
        voice.is_sustained = false;
        voice.patch = 13;
        voice.velocity = 0;
        voice.age = 0;
    }
    for (int i = 0; i < channel_count; ++i) {
        Channel& channel = channels_[static_cast<std::size_t>(i)];
        channel.patch = 13;
        channel.mapped_voices = 0;
        const int count = static_cast<int>(next() & 0x7FU);
        const std::uint8_t flags = next();
        if ((flags & 0x04U) != 0 && count != 0) {
            assign_voices(i, count);
        }
    }
}

void AdlibDriver::on_timer() {
    // The music timer has run (the OPL callback); the notes age.
    for (Voice& voice : voices_) {
        if (voice.note != -1) {
            ++voice.age;
        }
    }
}

void AdlibDriver::voice_mapping(int channel, int voices) {
    int current = 0;
    for (const Voice& voice : voices_) {
        if (voice.mapped_channel == channel) {
            ++current;
        }
    }
    // No extra voices in SCI0.
    if (current < voices) {
        assign_voices(channel, voices - current);
    } else if (current > voices) {
        release_voices(channel, current - voices);
        // donateVoices does nothing in SCI0.
    }
}

void AdlibDriver::assign_voices(int channel, int voices) {
    for (int i = 0; i < voice_count; ++i) {
        Voice& voice = voices_[static_cast<std::size_t>(i)];
        if (voice.mapped_channel == -1) {
            if (voice.note != -1) {
                voice_off(i);
            }
            voice.mapped_channel = channel;
            ++channels_[static_cast<std::size_t>(channel)].mapped_voices;
            if (--voices == 0) {
                return;
            }
        }
    }
    // SCI0 does not keep the voices it could not assign.
}

void AdlibDriver::release_voices(int channel, int voices) {
    Channel& state = channels_[static_cast<std::size_t>(channel)];
    // First the voices without a note, then those playing.
    for (const bool playing : {false, true}) {
        for (int i = 0; i < voice_count; ++i) {
            Voice& voice = voices_[static_cast<std::size_t>(i)];
            if (voice.mapped_channel == channel && (playing || voice.note == -1)) {
                if (playing) {
                    voice_off(i);
                }
                voice.mapped_channel = -1;
                --state.mapped_voices;
                if (--voices == 0) {
                    return;
                }
            }
        }
    }
}

void AdlibDriver::renew_notes(int channel, bool key) {
    for (int i = 0; i < voice_count; ++i) {
        const Voice& voice = voices_[static_cast<std::size_t>(i)];
        if ((channel == -1 || voice.channel == channel) && voice.note != -1) {
            set_note(i, voice.note, key);
        }
    }
}

void AdlibDriver::note_on(int channel, int note, int velocity) {
    if (velocity == 0) {
        note_off(channel, note);
        return;
    }
    velocity /= 2; // Non-negative: a shift right by 1.
    if (note < 12 || note > 107) {
        return; // Not playable.
    }
    for (int i = 0; i < voice_count; ++i) {
        const Voice& voice = voices_[static_cast<std::size_t>(i)];
        if (voice.channel == channel && voice.note == note) {
            voice_off(i);
            voice_on(i, note, velocity);
            return;
        }
    }
    const int voice = find_voice(channel);
    if (voice == -1) {
        return; // No free voice assigned to the channel.
    }
    voice_on(voice, note, velocity);
}

int AdlibDriver::find_voice(int channel) {
    Channel& state = channels_[static_cast<std::size_t>(channel)];
    int found = -1;
    int oldest_voice = -1;
    std::uint32_t oldest_age = 0;
    // A free voice assigned to the channel, round robin; failing that the
    // oldest note, but not one started in this timer period.
    for (int i = 0; i < voice_count; ++i) {
        const int v = (state.last_voice + i + 1) % voice_count;
        Voice& voice = voices_[static_cast<std::size_t>(v)];
        if (voice.mapped_channel == channel) {
            if (voice.note == -1) {
                found = v;
                voice.channel = channel;
                break;
            }
            if (voice.age >= oldest_age) {
                oldest_age = voice.age;
                oldest_voice = v;
            }
        }
    }
    if (found == -1) {
        if (oldest_age == 0) {
            return -1;
        }
        voice_off(oldest_voice);
        found = oldest_voice;
        voices_[static_cast<std::size_t>(found)].channel = channel;
    }
    state.last_voice = static_cast<std::uint8_t>(found);
    return found;
}

void AdlibDriver::note_off(int channel, int note) {
    for (int i = 0; i < voice_count; ++i) {
        Voice& voice = voices_[static_cast<std::size_t>(i)];
        if (voice.channel == channel && voice.note == note) {
            if (channels_[static_cast<std::size_t>(channel)].hold_pedal != 0) {
                voice.is_sustained = true;
            } else {
                voice_off(i);
            }
            return;
        }
    }
}

void AdlibDriver::voice_on(int voice, int note, int velocity) {
    Voice& state = voices_[static_cast<std::size_t>(voice)];
    Channel& channel = channels_[static_cast<std::size_t>(state.channel)];
    const int patch = channel.patch;
    state.age = 0;
    ++channel.voices;
    if (patch != state.patch && play_switch_) {
        set_patch(voice, patch);
    }
    state.velocity = static_cast<std::uint8_t>(velocity);
    set_note(voice, note, true);
}

void AdlibDriver::voice_off(int voice) {
    Voice& state = voices_[static_cast<std::size_t>(voice)];
    state.is_sustained = false;
    set_note(voice, state.note, false);
    state.note = -1;
    state.age = 0;
    if (state.channel != -1) {
        --channels_[static_cast<std::size_t>(state.channel)].voices;
    }
}

void AdlibDriver::set_note(int voice, int note, bool key) {
    Voice& state = voices_[static_cast<std::size_t>(voice)];
    state.note = note;
    if (state.channel == -1) {
        return; // A voice that never played; out of bounds in ScummVM.
    }
    int index = note * 4;
    int pitch_wheel = channels_[static_cast<std::size_t>(state.channel)].pitch_wheel;
    int sign = 0;
    if (pitch_wheel == 0x2000) {
        pitch_wheel = 0;
    } else if (pitch_wheel > 0x2000) {
        pitch_wheel -= 0x2000;
        sign = 1;
    } else {
        pitch_wheel = 0x2000 - pitch_wheel;
        sign = -1;
    }
    pitch_wheel /= 171;
    if (sign == 1) {
        index += pitch_wheel;
    } else {
        index -= pitch_wheel;
    }
    index = std::min(index, 0x1FC); // The highest MIDI note.
    index = std::max(index, 0);     // Not in SSCI.
    const int freq = adlib_freq[static_cast<std::size_t>(index % 48)];
    set_register(0xA0 + bits(voice), bits(freq) & 0xFFU);
    int octave = index / 48;
    if (octave > 0) {
        --octave;
    }
    octave = std::min(octave, 7); // Not in SSCI.
    set_register(0xB0 + bits(voice), ((key ? 1U : 0U) << 5U) | (bits(octave) << 2U) | (bits(freq) >> 8U));
    set_velocity(voice);
}

void AdlibDriver::set_velocity(int voice) {
    const Voice& state = voices_[static_cast<std::size_t>(voice)];
    if (state.patch < 0 || std::cmp_greater_equal(state.patch, patches_.size())) {
        return; // Out of bounds in ScummVM; only before the first track init.
    }
    const AdlibPatch& patch = patches_[static_cast<std::size_t>(state.patch)];
    const int pan = channels_[static_cast<std::size_t>(state.channel)].pan;
    const int offset = register_offset[static_cast<std::size_t>(voice)];
    set_velocity_reg(offset + 3, calc_velocity(voice, 1), patch.op[1].kb_scale_level, pan);
    // In AM mode both operators sound.
    if (patch.algorithm) {
        set_velocity_reg(offset, calc_velocity(voice, 0), patch.op[0].kb_scale_level, pan);
    }
}

int AdlibDriver::calc_velocity(int voice, int op) const {
    // SCI0: the master volume, raised by 3, scales the note velocity (with
    // controller 0x4E) or the instrument's level.
    int velocity = master_volume_;
    if (velocity > 0) {
        velocity += 3;
    }
    velocity = std::min(velocity, 15);
    const Voice& state = voices_[static_cast<std::size_t>(voice)];
    int instrument_velocity = 0;
    if (channels_[static_cast<std::size_t>(state.channel)].enable_velocity) {
        instrument_velocity = state.velocity;
    } else {
        instrument_velocity =
            63 - patches_[static_cast<std::size_t>(state.patch)].op[static_cast<std::size_t>(op)].total_level;
    }
    return velocity * instrument_velocity / 15;
}

void AdlibDriver::set_velocity_reg(int reg_offset, int velocity, int kb_scale_level, int pan) {
    if (!play_switch_) {
        velocity = 0;
    }
    // Stereo: the pan lowers one side.
    int left = velocity;
    int right = velocity;
    if (pan > 0x40) {
        left = left * (0x7F - pan) / 0x3F;
    } else if (pan < 0x40) {
        right = right * pan / 0x40;
    }
    const unsigned reg = 0x40 + bits(reg_offset);
    set_register(reg, (bits(kb_scale_level) << 6U) | bits(63 - left), Side::Left);
    set_register(reg, (bits(kb_scale_level) << 6U) | bits(63 - right), Side::Right);
}

void AdlibDriver::set_patch(int voice, int patch) {
    if (patch < 0 || std::cmp_greater_equal(patch, patches_.size())) {
        warn(std::format("ADLIB: Invalid patch {} requested", patch));
        patch = 0; // Instrument 0 instead.
    }
    voices_[static_cast<std::size_t>(voice)].patch = patch;
    const AdlibPatch& data = patches_[static_cast<std::size_t>(patch)];
    const unsigned offset = register_offset[static_cast<std::size_t>(voice)];
    set_operator(offset, data.op[0]);
    set_operator(offset + 3, data.op[1]);
    set_register(0xC0 + bits(voice), (bits(data.feedback) << 1U) | (data.algorithm ? 1U : 0U));
}

void AdlibDriver::set_operator(unsigned reg, const AdlibOperator& op) {
    const auto bit = [](bool flag, unsigned shift) { return (flag ? 1U : 0U) << shift; };
    set_register(0x40 + reg, (bits(op.kb_scale_level) << 6U) | op.total_level);
    set_register(0x60 + reg, (bits(op.attack_rate) << 4U) | op.decay_rate);
    set_register(0x80 + reg, (bits(op.sustain_level) << 4U) | op.release_rate);
    set_register(0x20 + reg, bit(op.amplitude_mod, 7) | bit(op.vibrato, 6) | bit(op.envelope_type, 5) |
                                 bit(op.kb_scale_rate, 4) | op.frequency_mult);
    set_register(0xE0 + reg, op.wave_form);
}

void AdlibDriver::set_register(unsigned reg, unsigned value, Side sides) {
    // Ports 0x220/0x221 are the left OPL2, 0x222/0x223 the right one.
    if (sides != Side::Right) {
        dual_write(false, reg, value);
    }
    if (sides != Side::Left) {
        dual_write(true, reg, value);
    }
}

void AdlibDriver::dual_write(bool right, unsigned reg, unsigned value) {
    // OPL::dualWrite: no OPL3 features on the two OPL2 halves.
    reg &= 0xFFU;
    value &= 0xFFU;
    if (reg == 5) {
        return;
    }
    if (reg >= 0xE0 && reg <= 0xE8) {
        value &= 3U; // Only the four OPL2 waveforms.
    }
    if (reg >= 0xC0 && reg <= 0xC8) {
        // Each half sounds on its side only.
        value &= 15U;
        value |= right ? 0xA0U : 0x50U;
    }
    const auto full = static_cast<std::uint16_t>(reg + (right ? 0x100U : 0U));
    opl_.write(full, static_cast<std::uint8_t>(value));
    if (register_log_ != nullptr) {
        register_log_->push_back({.reg = full, .value = static_cast<std::uint8_t>(value)});
    }
}

void AdlibDriver::play_switch(bool play) {
    play_switch_ = play;
    renew_notes(-1, play);
}

} // namespace sci::audio
