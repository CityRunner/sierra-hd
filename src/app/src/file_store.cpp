// SPDX-License-Identifier: GPL-3.0-or-later

#include "file_store.hpp"

#include "sci/core/log.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>

namespace sci::app {

namespace {

std::string lower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return result;
}

} // namespace

DirectoryFileStore::DirectoryFileStore(std::filesystem::path folder, const std::vector<resource::GameFile>& game_files)
    : folder_(std::move(folder)) {
    for (const resource::GameFile& file : game_files) {
        game_files_.emplace(lower(file.name), file.bytes);
    }
}

std::filesystem::path DirectoryFileStore::path_of(std::string_view name) const {
    // Only plain names: the scripts see no directories.
    std::string safe;
    for (const char c : name) {
        safe += (c == '/' || c == '\\' || c == ':') ? '_' : c;
    }
    return folder_ / safe;
}

std::optional<std::vector<std::uint8_t>> DirectoryFileStore::read(std::string_view name) {
    std::ifstream file(path_of(name), std::ios::binary);
    if (file) {
        return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
    if (const auto found = game_files_.find(lower(name)); found != game_files_.end()) {
        return found->second;
    }
    return std::nullopt;
}

bool DirectoryFileStore::write(std::string_view name, std::span<const std::uint8_t> data) {
    std::error_code error;
    std::filesystem::create_directories(folder_, error);
    std::ofstream file(path_of(name), std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(
                   data.size())); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): bytes to the stream.
    if (!file) {
        core::log(core::LogLevel::Warning, core::LogCategory::General,
                  std::format("Cannot write {}", path_of(name).string()));
        return false;
    }
    return true;
}

} // namespace sci::app
