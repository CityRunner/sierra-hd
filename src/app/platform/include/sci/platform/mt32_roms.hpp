// SPDX-License-Identifier: GPL-3.0-or-later
//
// The user's MT-32 ROMs (spec section 8): the folder named by mt32_rom in
// the configuration. Every file in it whose name ends in ".rom" (in any
// case) and that is no larger than 1 MiB is read and identified by its
// contents, as mt32emu identifies ROMs (size and SHA-1), so the file names
// do not matter otherwise: ScummVM's MT32_CONTROL.ROM and MT32_PCM.ROM,
// Munt's or the dumps' own names all work. Of the control ROMs found, the
// MT-32 1.07 is preferred (the game was written for an MT-32), then the
// other MT-32 versions, then the CM-32L; each is paired with a PCM ROM of
// its model. Files are taken in name order where the preference is equal.
#pragma once

#include "sci/audio/mt32.hpp"
#include "sci/core/error.hpp"
#include "sci/platform/config.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace sci::platform {

// The largest ROM: the CM-32L's PCM ROM (1 MiB).
inline constexpr std::uintmax_t max_mt32_rom_size = std::uintmax_t{1} << 20U;

struct Mt32Selection {
    std::unique_ptr<audio::Mt32> synth;
    std::filesystem::path control;
    std::filesystem::path pcm;
};

// Opens an MT-32 on the best pair of ROMs in `folder`. NotFound when the
// folder is empty (not configured), missing, or holds no control ROM or no
// PCM ROM for it that mt32emu knows; the error of Mt32::open when no pair
// opens.
[[nodiscard]] core::Result<Mt32Selection> open_mt32_roms(const std::filesystem::path& folder);

// The preference of a control ROM by mt32emu's short name: 0 for the MT-32
// 1.07, 1 for another MT-32 version, 2 for the CM-32L (and CM-32LN).
[[nodiscard]] int mt32_control_rank(std::string_view name);

// The music device the program plays on (spec section 8): the configured
// one, except that the MT-32 falls back to the AdLib when its ROMs cannot
// be opened; `log` then says why (a warning), or which ROMs play (info).
struct MusicChoice {
    MusicDevice device = MusicDevice::Adlib;
    std::unique_ptr<audio::Mt32> mt32; // Set when device is Mt32.
    std::string log;
    bool warning = false;
};

[[nodiscard]] MusicChoice choose_music_device(const Config& config);

} // namespace sci::platform
