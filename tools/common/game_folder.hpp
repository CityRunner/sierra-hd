// SPDX-License-Identifier: GPL-3.0-or-later
//
// File access shared by the development tools: reading a game folder for
// sci-resource and writing output files.

#pragma once

#include "sci/resource/resource_manager.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace sci::tools {

// Opens the game in the folder; prints the reason and returns nullopt on
// failure.
[[nodiscard]] std::optional<resource::ResourceManager> open_game(const std::filesystem::path& folder);

[[nodiscard]] bool write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

// Prints log messages and failed assertions to stderr and enables the
// resource log category.
void install_stderr_log();

} // namespace sci::tools
