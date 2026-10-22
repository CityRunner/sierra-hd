// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files the game opens (kFOpen), for the program: what the game writes
// goes into a folder next to the configuration, as ScummVM keeps them in
// its save folder; what it reads comes from there, or else from the game
// folder.
#pragma once

#include "sci/resource/resource_manager.hpp"
#include "sci/vm/files.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace sci::app {

class DirectoryFileStore final : public vm::FileStore {
public:
    // The game folder's files, by lower-case name, are kept for reading.
    DirectoryFileStore(std::filesystem::path folder, const std::vector<resource::GameFile>& game_files);

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read(std::string_view name) override;
    bool write(std::string_view name, std::span<const std::uint8_t> data) override;

private:
    [[nodiscard]] std::filesystem::path path_of(std::string_view name) const;

    std::filesystem::path folder_;
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> game_files_;
};

} // namespace sci::app
