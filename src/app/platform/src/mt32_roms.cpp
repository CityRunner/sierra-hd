// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/mt32_roms.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace sci::platform {

namespace {

namespace fs = std::filesystem;

struct RomFile {
    fs::path path;
    std::vector<std::uint8_t> bytes;
    audio::Mt32RomInfo info;
};

bool is_rom_name(const fs::path& path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return extension == ".rom";
}

std::optional<std::vector<std::uint8_t>> read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) {
        return std::nullopt;
    }
    return bytes;
}

} // namespace

int mt32_control_rank(std::string_view name) {
    if (name == "ctrl_mt32_1_07") {
        return 0;
    }
    return name.starts_with("ctrl_cm32l") ? 2 : 1;
}

core::Result<Mt32Selection> open_mt32_roms(const fs::path& folder) {
    if (folder.empty()) {
        return core::make_error(core::ErrorCode::NotFound, "no MT-32 ROM folder is configured (mt32_rom)");
    }
    std::error_code error;
    if (!fs::is_directory(folder, error)) {
        return core::make_error(core::ErrorCode::NotFound,
                                std::format("the MT-32 ROM folder {} does not exist", folder.string()));
    }
    std::vector<fs::path> paths;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder, error)) {
        std::error_code entry_error;
        if (entry.is_regular_file(entry_error) && is_rom_name(entry.path()) &&
            entry.file_size(entry_error) <= max_mt32_rom_size && !entry_error) {
            paths.push_back(entry.path());
        }
    }
    // Paths in one folder are distinct.
    std::ranges::stable_sort(paths);
    std::vector<RomFile> controls;
    std::vector<RomFile> pcms;
    for (const fs::path& path : paths) {
        auto bytes = read_file(path);
        if (!bytes) {
            continue;
        }
        auto info = audio::identify_mt32_rom(*bytes);
        if (!info) {
            continue; // Not a ROM mt32emu knows, such as a patched one.
        }
        const bool control = info->control;
        (control ? controls : pcms).push_back({.path = path, .bytes = std::move(*bytes), .info = std::move(*info)});
    }
    std::ranges::stable_sort(controls, {}, [](const RomFile& rom) { return mt32_control_rank(rom.info.name); });
    if (controls.empty()) {
        return core::make_error(core::ErrorCode::NotFound,
                                std::format("no MT-32 or CM-32L control ROM in {}", folder.string()));
    }
    std::optional<core::Error> last_error;
    for (const RomFile& control : controls) {
        for (const RomFile& pcm : pcms) {
            if (pcm.info.model != control.info.model) {
                continue;
            }
            auto synth = audio::Mt32::open(control.bytes, pcm.bytes);
            if (synth) {
                return Mt32Selection{.synth = std::move(*synth), .control = control.path, .pcm = pcm.path};
            }
            last_error = std::move(synth.error());
        }
    }
    if (last_error) {
        return std::unexpected(std::move(*last_error));
    }
    return core::make_error(core::ErrorCode::NotFound, std::format("no PCM ROM for the {} in {}",
                                                                   controls.front().info.description, folder.string()));
}

MusicChoice choose_music_device(const Config& config) {
    if (config.music != MusicDevice::Mt32) {
        return MusicChoice{.device = config.music, .mt32 = nullptr, .log = {}, .warning = false};
    }
    auto selection = open_mt32_roms(config.mt32_rom);
    if (!selection) {
        return MusicChoice{
            .device = MusicDevice::Adlib,
            .mt32 = nullptr,
            .log = std::format("music: no MT-32 ({}); playing on the AdLib", selection.error().message),
            .warning = true,
        };
    }
    const audio::Mt32Roms& roms = selection->synth->roms();
    std::string log =
        std::format("music: {} on the {} ({}) and the {} ({})",
                    roms.model == audio::Mt32Model::Mt32 ? "MT-32" : "CM-32L", roms.control_description,
                    selection->control.filename().string(), roms.pcm_description, selection->pcm.filename().string());
    return MusicChoice{
        .device = MusicDevice::Mt32,
        .mt32 = std::move(selection->synth),
        .log = std::move(log),
        .warning = false,
    };
}

} // namespace sci::platform
