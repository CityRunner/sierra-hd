// SPDX-License-Identifier: GPL-3.0-or-later
//
// The configuration file next to the executable (spec section 10.2): an
// INI file with a format version.
//
//   [sierra-hd]
//   format = 1
//   game_folder = C:\Games\Colonel's Bequest
//   mode = hd          ; hd or original
//   scale = auto       ; auto, or N from 1 to 16
//   priority_snap = off ; on: sprites are hidden behind the picture in
//                       ; whole 1x pixels, as in the original
//   mt32_rom =
//   music = adlib      ; adlib, mt32 or off
//   volume = 100       ; 0 to 100 (per cent)
//   saves =            ; the folder of the saved games
//
// Lines starting with ';' or '#' are comments, and so is the rest of a
// line after ';' or '#', except in the paths (game_folder, mt32_rom, the
// folder of the MT-32 ROMs, and saves), which may contain those
// characters. Unknown keys are reported and ignored, so that a newer
// file still loads; a missing key keeps its default, so that an older file
// loads too.
#pragma once

#include "sci/core/error.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sci::platform {

inline constexpr int config_format = 1;
inline constexpr int max_scale = 16;
inline constexpr int max_volume = 100;

enum class GraphicsMode : std::int8_t { Hd, Original };

// The music device (milestone 7, spec section 8). Off plays the music on
// the silent driver: the game's timing, signals and cues are the same, with
// no sound. Mt32 needs the ROMs in mt32_rom and falls back to AdLib without
// them.
enum class MusicDevice : std::int8_t { Adlib, Mt32, Off };

struct Config {
    std::filesystem::path game_folder;
    GraphicsMode mode = GraphicsMode::Hd;
    int scale = 0; // 0: the largest N that fits.
    // HD occlusion edges snapped to the sprite grid (spec section 6.6).
    bool priority_snap = false;
    std::filesystem::path mt32_rom;
    MusicDevice music = MusicDevice::Adlib;
    // The output volume in per cent.
    int volume = max_volume;
    // The folder of the saved games; empty: `saves` next to the
    // configuration (saves_folder).
    std::filesystem::path saves;
};

// The folder of the saved games: `saves` in the configuration's folder by
// default; a relative path is taken from the configuration's folder.
[[nodiscard]] std::filesystem::path saves_folder(const Config& config, const std::filesystem::path& config_folder);

struct ParsedConfig {
    Config config;
    std::vector<std::string> warnings;
};

[[nodiscard]] core::Result<ParsedConfig> parse_config(std::string_view text);

[[nodiscard]] std::string format_config(const Config& config);

} // namespace sci::platform
