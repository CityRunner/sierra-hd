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
//   mt32_rom =
//
// Lines starting with ';' or '#' are comments, and so is the rest of a
// line after ';' or '#', except in the paths (game_folder, and mt32_rom,
// the folder of the MT-32 ROMs, used from milestone 7), which may contain
// those characters. Unknown keys are reported and ignored, so that a newer
// file still loads.
#pragma once

#include "sci/core/error.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sci::platform {

inline constexpr int config_format = 1;
inline constexpr int max_scale = 16;

enum class GraphicsMode : std::int8_t { Hd, Original };

struct Config {
    std::filesystem::path game_folder;
    GraphicsMode mode = GraphicsMode::Hd;
    int scale = 0; // 0: the largest N that fits.
    std::filesystem::path mt32_rom;
};

struct ParsedConfig {
    Config config;
    std::vector<std::string> warnings;
};

[[nodiscard]] core::Result<ParsedConfig> parse_config(std::string_view text);

[[nodiscard]] std::string format_config(const Config& config);

} // namespace sci::platform
