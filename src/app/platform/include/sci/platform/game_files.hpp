// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading a game folder from disk, for the program and the tools.
#pragma once

#include "sci/core/error.hpp"
#include "sci/resource/resource_manager.hpp"

#include <filesystem>
#include <vector>

namespace sci::platform {

// The regular files directly in the folder, sorted by name; the game does
// not use subdirectories.
[[nodiscard]] core::Result<std::vector<resource::GameFile>> read_game_folder(const std::filesystem::path& folder);

} // namespace sci::platform
