// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/save_folder.hpp"

#include "sci/core/log.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>

namespace sci::platform {

namespace {

constexpr std::string_view prefix = "sierra-hd-";
constexpr std::string_view suffix = ".sav";

// The slot of a file name of the folder, or nothing.
std::optional<int> slot_of(const std::string& name) {
    if (name.size() != prefix.size() + 3 + suffix.size() || !name.starts_with(prefix) || !name.ends_with(suffix)) {
        return std::nullopt;
    }
    int slot = 0;
    for (const char c : name.substr(prefix.size(), 3)) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        slot = (slot * 10) + (c - '0');
    }
    if (slot < vm::save_slot_first || slot > vm::save_slot_last) {
        return std::nullopt;
    }
    return slot;
}

} // namespace

std::string SaveFolder::file_name(int slot) {
    return std::format("{}{:03}{}", prefix, slot, suffix);
}

std::vector<int> SaveFolder::slots() {
    std::vector<int> result;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder_, error)) {
        if (!entry.is_regular_file(error)) {
            continue;
        }
        if (const auto slot = slot_of(entry.path().filename().string())) {
            result.push_back(*slot);
        }
    }
    std::ranges::stable_sort(result); // Slots are distinct.
    return result;
}

std::optional<std::vector<std::uint8_t>> SaveFolder::read(int slot) {
    std::ifstream file(folder_ / file_name(slot), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool SaveFolder::write(int slot, std::span<const std::uint8_t> data) {
    std::error_code error;
    std::filesystem::create_directories(folder_, error);
    const std::filesystem::path path = folder_ / file_name(slot);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): bytes to the stream.
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    file.close();
    if (!file) {
        core::log(core::LogLevel::Warning, core::LogCategory::General, std::format("Cannot write {}", path.string()));
        return false;
    }
    return true;
}

} // namespace sci::platform
