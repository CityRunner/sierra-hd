// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/sound/drivers/midi.cpp (MidiPlayer_Midi, SCI0 late, an
//     emulated MT-32)
//   audio/softsynth/mt32.cpp (MidiDriver_MT32::sysEx)
// and translated into this project's style, types and error handling.
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

#include "sci/audio/mt32_driver.hpp"

#include "sci/core/assert.hpp"
#include "sci/core/log.hpp"
#include "sci/resource/resource_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace sci::audio {

namespace {

// The bytes before the timbres: three texts, the volume, the default
// reverb, a reverb SysEx, the reverb settings and patches 1-48.
constexpr std::size_t header_size = 491;
constexpr std::size_t patches_49_96_size = 256 + 128;
constexpr std::size_t rhythm_size = 256 + 9;
// The largest MT-32 patch: 64 timbres and both optional parts.
constexpr std::size_t max_mt32_size =
    header_size + 1 + (64 * Mt32Patch::timbre_size) + 2 + patches_49_96_size + 2 + rhythm_size;
// The General MIDI format: 1153 bytes of maps, then a length and MIDI data.
constexpr std::size_t gm_map_size = 1153;
constexpr std::uint16_t flag_patches_49_96 = 0xABCD;
constexpr std::uint16_t flag_rhythm = 0xDCBA;
// The SCI driver's SysEx header: Roland, device 0x10 (the unit), MT-32,
// data set 1 (DT1).
constexpr std::uint8_t sysex_device = 0x10;
constexpr std::size_t max_sysex_size = 264;

// A channel message's status byte.
unsigned status(unsigned command, int channel) {
    return command | static_cast<unsigned>(channel);
}

void warn(std::string_view message) {
    core::log(core::LogLevel::Warning, core::LogCategory::General, message);
}

std::uint16_t u16le(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint16_t>(unsigned{data[at]} | (unsigned{data[at + 1]} << 8U));
}

std::uint16_t u16be(std::span<const std::uint8_t> data, std::size_t at) {
    return static_cast<std::uint16_t>((unsigned{data[at]} << 8U) | unsigned{data[at + 1]});
}

template <std::size_t N>
std::array<std::uint8_t, N> take(std::span<const std::uint8_t> data, std::size_t& at) {
    std::array<std::uint8_t, N> out{};
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(at), N, out.begin());
    at += N;
    return out;
}

// MidiPlayer_Midi::isMt32GmPatch: true for the General MIDI format; an
// error when the data is neither format.
core::Result<bool> is_gm_patch(std::span<const std::uint8_t> data) {
    std::size_t size = data.size();
    // An extra byte at the end in some Mac games (ScummVM bug #6725).
    if (size == max_mt32_size + 1) {
        --size;
    }
    if (size < gm_map_size + 2) {
        return false;
    }
    if (size > max_mt32_size) {
        return true;
    }
    const bool is_gm = u16le(data, gm_map_size) + gm_map_size + 2 == size;
    std::size_t pos = header_size + 1 + (std::size_t{data[header_size]} * Mt32Patch::timbre_size);
    if (size >= pos + patches_49_96_size + 2 && u16be(data, pos) == flag_patches_49_96) {
        pos += patches_49_96_size + 2;
    }
    if (size >= pos + rhythm_size + 2 && u16be(data, pos) == flag_rhythm) {
        pos += rhythm_size + 2;
    }
    const bool is_mt32 = size == pos;
    if (is_mt32 == is_gm) {
        return core::make_error(core::ErrorCode::InvalidData, "Failed to detect MT-32 patch format");
    }
    return is_gm;
}

} // namespace

core::Result<Mt32Patch> parse_mt32_patch(std::span<const std::uint8_t> data) {
    const auto gm = is_gm_patch(data);
    if (!gm) {
        return std::unexpected(gm.error());
    }
    if (*gm) {
        return core::make_error(core::ErrorCode::Unsupported, "an MT-32 patch in the General MIDI format");
    }
    if (data.size() < header_size + 1 ||
        data.size() < header_size + 1 + (std::size_t{data[header_size]} * Mt32Patch::timbre_size)) {
        return core::make_error(core::ErrorCode::InvalidData,
                                std::format("the MT-32 patch ({} bytes) is shorter than its timbres", data.size()));
    }
    Mt32Patch patch;
    std::size_t at = 0;
    patch.after_text = take<Mt32Patch::text_size>(data, at);
    patch.before_text = take<Mt32Patch::text_size>(data, at);
    patch.goodbye_text = take<Mt32Patch::text_size>(data, at);
    patch.volume = std::min<std::uint16_t>(u16le(data, at), 100);
    at += 2;
    patch.default_reverb = static_cast<std::int8_t>(data[at++]);
    at += 11; // The reverb SysEx message, not sent.
    // The reverb settings are stored by column (ScummVM trac #9261).
    for (std::size_t j = 0; j < 3; ++j) {
        for (std::size_t i = 0; i < Mt32Patch::reverb_configs; ++i) {
            patch.reverb[i][j] = data[at++];
        }
    }
    patch.patches_1_48_low = take<256>(data, at);
    patch.patches_1_48_high = take<128>(data, at);
    const std::size_t timbres = data[at++];
    for (std::size_t i = 0; i < timbres; ++i) {
        patch.timbres.push_back(take<Mt32Patch::timbre_size>(data, at));
    }
    const auto flag = [&] -> std::optional<std::uint16_t> {
        if (data.size() - at < 2) {
            return std::nullopt;
        }
        at += 2;
        return u16be(data, at - 2);
    };
    std::optional<std::uint16_t> next = flag();
    if (next == flag_patches_49_96 && data.size() - at >= patches_49_96_size) {
        patch.patches_49_96 = take<patches_49_96_size>(data, at);
        next = flag();
    }
    if (next == flag_rhythm && data.size() - at >= rhythm_size) {
        patch.rhythm_key_map = take<256>(data, at);
        patch.partial_reserve = take<9>(data, at);
    }
    return patch;
}

core::Result<std::optional<Mt32Patch>> load_mt32_patch(const resource::ResourceManager& resources) {
    const resource::ResourceId id{.type = resource::ResourceType::Patch, .number = 1};
    if (!resources.contains(id)) {
        return std::optional<Mt32Patch>();
    }
    auto data = resources.load(id);
    if (!data) {
        return std::unexpected(std::move(data.error()));
    }
    auto patch = parse_mt32_patch(data->data);
    if (!patch) {
        return std::unexpected(std::move(patch.error()));
    }
    return std::optional<Mt32Patch>(std::move(*patch));
}

// Mt32Driver -------------------------------------------------------------------------

Mt32Driver::Mt32Driver(std::unique_ptr<Mt32> synth, const std::optional<Mt32Patch>& patch, Log* log)
    : synth_(std::move(synth)), log_(log) {
    // resetMt32: an emulated MT-32 needs no wait after it.
    send_sysex(0x7F0000, std::array<std::uint8_t, 2>{0x01, 0x00});
    if (patch) {
        send_patch(*patch);
    }
}

Mt32Driver::~Mt32Driver() {
    // MidiPlayer_Midi::close. Without a patch ScummVM's goodbye text is
    // uninitialised; nothing is sent then (has_reverb_ is set by the patch).
    if (has_reverb_) {
        send_sysex(0x200000, goodbye_text_);
    }
}

Mt32& Mt32Driver::synth() {
    SCI_ASSERT(synth_ != nullptr);
    return *synth_;
}

void Mt32Driver::play(std::uint32_t message) {
    if (log_ != nullptr) {
        log_->midi.push_back(message);
        if (log_->on_send) {
            const std::array<std::uint8_t, 3> bytes{
                static_cast<std::uint8_t>(message & 0xFFU),
                static_cast<std::uint8_t>((message >> 8U) & 0xFFU),
                static_cast<std::uint8_t>((message >> 16U) & 0xFFU),
            };
            const unsigned command = message & 0xF0U;
            log_->on_send(std::span(bytes).first(command == 0xC0U || command == 0xD0U ? 2 : 3));
        }
    }
    if (synth_ != nullptr) {
        synth_->play_now(message);
    }
}

void Mt32Driver::write_sysex(std::uint8_t device, std::span<const std::uint8_t> address_and_data) {
    if (log_ != nullptr) {
        log_->sysex.emplace_back(address_and_data.begin(), address_and_data.end());
        log_->sysex_at.push_back(log_->midi.size());
    }
    if (synth_ != nullptr) {
        synth_->write_sysex(device, address_and_data);
    }
}

PlayerChoices Mt32Driver::player_choices() const {
    return {.play_id = play_id, .polyphony = polyphony, .has_rhythm_channel = has_rhythm_channel};
}

void Mt32Driver::send_sysex(std::uint32_t address, std::span<const std::uint8_t> data) {
    // sendMt32SysEx: the address and data, then the checksum, which
    // MidiDriver_MT32::sysEx leaves out when it writes them into the
    // synthesiser (and which mt32emu would not check there).
    if (data.size() + 8 > max_sysex_size) {
        warn("SysEx message exceed maximum size; ignoring");
        return;
    }
    std::vector<std::uint8_t> message{
        0x41,
        sysex_device,
        0x16,
        0x12,
        static_cast<std::uint8_t>((address >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((address >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(address & 0xFFU),
    };
    message.insert(message.end(), data.begin(), data.end());
    unsigned checksum = 0;
    for (std::size_t i = 4; i < message.size(); ++i) {
        checksum -= message[i];
    }
    message.push_back(static_cast<std::uint8_t>(checksum & 0x7FU));
    if (log_ != nullptr && log_->on_send) {
        log_->on_send(message);
    }
    write_sysex(sysex_device, std::span<const std::uint8_t>(message).subspan(4, message.size() - 5));
}

void Mt32Driver::send_patch(const Mt32Patch& patch) {
    // readMt32Patch, for the games after KQ4 and LSL2: the second text is
    // shown first.
    send_sysex(0x200000, patch.before_text);
    goodbye_text_ = patch.goodbye_text;
    send_sysex(0x100016, std::array<std::uint8_t, 1>{static_cast<std::uint8_t>(patch.volume)});
    default_reverb_ = patch.default_reverb;
    has_reverb_ = true;
    reverb_config_ = patch.reverb;
    send_sysex(0x050000, patch.patches_1_48_low);
    send_sysex(0x050200, patch.patches_1_48_high);
    for (std::size_t i = 0; i < patch.timbres.size(); ++i) {
        send_sysex(0x080000 + static_cast<std::uint32_t>(i << 9U), patch.timbres[i]);
    }
    if (patch.patches_49_96) {
        const std::span<const std::uint8_t> patches(*patch.patches_49_96);
        send_sysex(0x050300, patches.first(256));
        send_sysex(0x050500, patches.subspan(256));
    }
    if (patch.rhythm_key_map) {
        send_sysex(0x030110, *patch.rhythm_key_map);
        send_sysex(0x100004, patch.partial_reserve);
    }
    send_sysex(0x200000, patch.after_text);
    // ScummVM's "mystery SysEx", for the MT-32 (not the D-110).
    send_sysex(0x52000A, std::array<std::uint8_t, 6>{0x16, 0x16, 0x16, 0x16, 0x16, 0x16});
}

void Mt32Driver::send_midi(unsigned status_byte, int first, int second) {
    play(status_byte | (static_cast<std::uint32_t>(first) << 8U) | (static_cast<std::uint32_t>(second) << 16U));
}

void Mt32Driver::note_on(int channel, int note, int velocity) {
    // The patch, key shift, velocity and percussion maps are the identity
    // for an MT-32 (they map to General MIDI); only an unmapped channel
    // (no program yet) drops its notes.
    if (channel != rhythm_channel && channels_[static_cast<std::size_t>(channel)].mapped_patch == unmapped) {
        return;
    }
    channels_[static_cast<std::size_t>(channel)].playing = true;
    send_midi(status(0x90U, channel), note, velocity);
}

void Mt32Driver::control_change(int channel, int control, int value) {
    Channel& state = channels_[static_cast<std::size_t>(channel)];
    switch (control) {
    case 0x07:
        state.volume = static_cast<std::uint8_t>(value);
        if (!play_switch_) {
            return;
        }
        // No volume adjustment for an MT-32 (it maps to General MIDI).
        value = std::min(value, 0x7F) * master_volume_;
        if (value != 0) {
            value = std::max(value / 15, 1);
        }
        break;
    case 0x0A:
        state.pan = static_cast<std::uint8_t>(value);
        break;
    case 0x40:
        state.hold = static_cast<std::uint8_t>(value);
        break;
    case 0x4B:
        // Sierra's voice mapping, not for a MIDI device (ScummVM #11409).
        return;
    case 0x7B:
        state.playing = false;
        break;
    default:
        break;
    }
    send_midi(status(0xB0U, channel), control, value);
}

void Mt32Driver::set_patch(int channel, int patch) {
    Channel& state = channels_[static_cast<std::size_t>(channel)];
    // The MT-32's rhythm part keeps its own setup.
    if (channel == rhythm_channel || std::cmp_equal(state.patch, patch)) {
        return;
    }
    state.patch = static_cast<std::uint8_t>(patch);
    // The first program sets the channel's volume too.
    const bool reset_volume = state.mapped_patch == unmapped;
    state.mapped_patch = static_cast<std::uint8_t>(patch);
    if (reset_volume) {
        control_change(channel, 0x07, state.volume);
    }
    send_midi(status(0xC0U, channel), patch, 0);
    // ScummVM's workaround for USB-MIDI cables, kept for the same messages.
    send_midi(status(0xB0U, channel), 0x0A, state.pan);
}

void Mt32Driver::send(std::uint32_t message) {
    const int command = static_cast<int>(message & 0xF0U);
    const int channel = static_cast<int>(message & 0x0FU);
    const int first = static_cast<int>((message >> 8U) & 0x7FU);
    const int second = static_cast<int>((message >> 16U) & 0x7FU);
    switch (command) {
    case 0x80:
        note_on(channel, first, 0);
        break;
    case 0x90:
        note_on(channel, first, second);
        break;
    case 0xB0:
        control_change(channel, first, second);
        break;
    case 0xC0:
        set_patch(channel, first);
        break;
    case 0xA0:
    case 0xD0:
        break; // Sierra's driver ignores aftertouch.
    case 0xE0:
        play(message);
        break;
    default:
        warn(std::format("Ignoring MIDI event {:02x}", command));
        break;
    }
}

void Mt32Driver::set_volume(std::uint8_t volume) {
    master_volume_ = volume;
    if (!play_switch_) {
        return;
    }
    for (int channel = 1; channel < 10; ++channel) {
        control_change(channel, 0x07, static_cast<int>(channels_[static_cast<std::size_t>(channel)].volume & 0x7FU));
    }
}

void Mt32Driver::set_reverb(std::int8_t reverb) {
    // ScummVM indexes its settings with any value; -1 (no reverb, from a
    // song's controller 0x50 = 127 before a global reverb is set) would
    // read outside them, and is only stored here.
    if (has_reverb_ && reverb_ != reverb && reverb >= 0 && std::cmp_less(reverb, reverb_config_.size())) {
        send_sysex(0x100001, reverb_config_[static_cast<std::uint8_t>(reverb)]);
    }
    reverb_ = reverb;
}

void Mt32Driver::play_switch(bool play) {
    play_switch_ = play;
    if (play) {
        set_volume(static_cast<std::uint8_t>(master_volume_));
        return;
    }
    for (int channel = 1; channel < 10; ++channel) {
        send_midi(status(0xB0U, channel), 0x07, 0);
    }
}

void Mt32Driver::init_track(std::span<const std::uint8_t> header) {
    if (default_reverb_ >= 0) {
        // SCI0 resets the reverb to the patch's default before every song.
        set_reverb(default_reverb_);
    }
    // SCI0 late: the play mask of channels 1-8 (byte 4, 6, ... 18) and the
    // rhythm flag 0x80 in channel 9's voice count (byte 19) give the
    // channels of the MT-32's parts 1-8 and of its rhythm part; 0x10 is no
    // channel.
    constexpr std::size_t late_header_size = 20;
    if (header.size() < late_header_size) {
        return;
    }
    const std::uint8_t caps = header[0];
    if (caps != 0 && caps != 2) {
        return;
    }
    std::array<std::uint8_t, 9> parts{};
    for (std::size_t channel = 1; channel < 9; ++channel) {
        parts[channel - 1] = (header[2 + (channel * 2)] & 1U) != 0 ? static_cast<std::uint8_t>(channel) : 0x10;
    }
    parts[8] = (header[19] & 0x80U) != 0 ? 9 : 0x10;
    send_sysex(0x10000D, parts);
}

void Mt32Driver::sysex(std::span<const std::uint8_t> message) {
    // MidiDriver_MT32::sysEx for an unframed message: a Roland DT1 (0x12)
    // or DAT (0x42) is written without its header and checksum.
    if (log_ != nullptr && log_->on_send) {
        log_->on_send(message);
    }
    if (!message.empty() && message[0] == 0xF0) {
        if (log_ != nullptr) {
            log_->sysex.emplace_back(message.begin(), message.end());
            log_->sysex_at.push_back(log_->midi.size());
        }
        if (synth_ != nullptr) {
            synth_->play_sysex_now(message);
        }
        return;
    }
    if (message.size() < 5 || (message[3] != 0x12 && message[3] != 0x42)) {
        warn(std::format("Unused sysEx command {}", message.size() < 4 ? -1 : message[3]));
        return;
    }
    write_sysex(message[1], message.subspan(4, message.size() - 5));
}

} // namespace sci::audio
