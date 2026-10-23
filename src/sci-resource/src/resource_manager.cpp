// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/resource/resource.cpp (ResourceManager::addAppropriateSources,
//       ResourceManager::init, readResourceMapSCI0, readResourcePatches,
//       processPatch, addResource, updateResource, validateResource,
//       ResourceSource::loadResource, Resource::decompress)
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

#include "sci/resource/resource_manager.hpp"

#include "ascii.hpp"
#include "sci/core/log.hpp"
#include "sci/resource/patch_file.hpp"
#include "sci/resource/resource_map.hpp"
#include "sci/resource/volume.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <utility>

namespace sci::resource {

namespace {

using core::ErrorCode;
using core::make_error;
using core::Result;

void log_resources(core::LogLevel level, std::string_view message) {
    core::log(level, core::LogCategory::Resources, message);
}

std::optional<std::size_t> find_file(const std::vector<GameFile>& files, std::string_view name) {
    const auto it =
        std::ranges::find_if(files, [name](const GameFile& file) { return ascii::iequals(file.name, name); });
    return it == files.end() ? std::nullopt : std::optional<std::size_t>(it - files.begin());
}

// "resource.0##": the volumes of SCI0 to SCI2, numbered by their extension.
std::optional<std::uint8_t> volume_number(std::string_view name) {
    constexpr std::string_view prefix = "resource.0";
    if (name.size() != prefix.size() + 2 || !ascii::istarts_with(name, prefix) || !ascii::is_digit(name[10]) ||
        !ascii::is_digit(name[11])) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(((name[10] - '0') * 10) + (name[11] - '0'));
}

} // namespace

// The volume files ordered by name, as ScummVM sorts them (case-sensitive).
std::vector<ResourceManager::VolumeFile> ResourceManager::find_volumes() const {
    std::vector<VolumeFile> volumes;
    for (std::size_t i = 0; i < files_.size(); ++i) {
        if (const auto number = volume_number(files_[i].name)) {
            volumes.push_back(VolumeFile{.file = i, .number = *number});
        }
    }
    std::ranges::stable_sort(volumes, [this](const VolumeFile& a, const VolumeFile& b) {
        return files_[a.file].name < files_[b.file].name;
    });
    return volumes;
}

const GameFile* ResourceManager::file(std::string_view name) const {
    const std::optional<std::size_t> found = find_file(files_, name);
    return found ? &files_[*found] : nullptr;
}

Result<ResourceManager> ResourceManager::open(std::vector<GameFile> files) {
    ResourceManager manager(std::move(files));
    const std::optional<std::size_t> map = find_file(manager.files_, "resource.map");
    if (!map) {
        return make_error(ErrorCode::NotFound, "the game folder has no RESOURCE.MAP");
    }
    const std::vector<VolumeFile> volumes = manager.find_volumes();
    if (volumes.empty()) {
        return make_error(ErrorCode::NotFound, "the game folder has no resource volumes (RESOURCE.0xx)");
    }

    // Format detection as in ResourceManager::init: the map, then the first
    // volume in name order.
    std::vector<std::uint8_t> volume_numbers;
    volume_numbers.reserve(volumes.size());
    for (const VolumeFile& volume : volumes) {
        volume_numbers.push_back(volume.number);
    }
    if (auto checked = check_sci0_map(manager.files_[*map].bytes, volume_numbers); !checked) {
        return std::unexpected(checked.error());
    }
    if (auto checked = check_sci0_volume(manager.files_[volumes.front().file].bytes); !checked) {
        return std::unexpected(checked.error());
    }

    // Sources are scanned in ScummVM's order: the map, then the patch files.
    if (auto indexed = manager.index_map(*map, volumes); !indexed) {
        return std::unexpected(indexed.error());
    }
    manager.apply_patches();
    return manager;
}

Result<void> ResourceManager::index_map(std::size_t map_file, std::span<const VolumeFile> volumes) {
    const auto entries = parse_sci0_map(files_[map_file].bytes);
    if (!entries) {
        return std::unexpected(entries.error());
    }
    for (const MapEntry& entry : *entries) {
        if (index_.contains(entry.id)) {
            continue;
        }
        const auto volume = std::ranges::find_if(
            volumes, [&](const VolumeFile& candidate) { return candidate.number == entry.volume; });
        if (volume == volumes.end()) {
            return make_error(ErrorCode::InvalidData,
                              std::format("resource map names volume {}, which does not exist", entry.volume));
        }
        // validateResource: an entry must start inside its volume.
        const GameFile& volume_file = files_[volume->file];
        if (entry.offset >= volume_file.bytes.size()) {
            log_resources(core::LogLevel::Warning,
                          std::format("{}.{} points beyond the end of {} ({} >= {}); skipped",
                                      resource_type_name(entry.id.type), entry.id.number, volume_file.name,
                                      entry.offset, volume_file.bytes.size()));
            continue;
        }
        index_[entry.id] = Location{.file = volume->file, .offset = entry.offset, .patch = false};
    }
    return {};
}

void ResourceManager::apply_patches() {
    std::vector<std::string> names;
    names.reserve(files_.size());
    for (const GameFile& file : files_) {
        names.push_back(file.name);
    }
    for (const PatchCandidate& candidate : find_patch_candidates(names)) {
        const GameFile& file = files_[candidate.file];
        const std::optional<std::size_t> offset = patch_data_offset(file.bytes, candidate.id.type);
        if (!offset) {
            log_resources(core::LogLevel::Debug, std::format("Patching {} failed", file.name));
            continue;
        }
        index_[candidate.id] =
            Location{.file = candidate.file, .offset = static_cast<std::uint32_t>(*offset), .patch = true};
        log_resources(core::LogLevel::Info, std::format("Patching {} - OK", file.name));
    }
}

std::vector<ResourceId> ResourceManager::list() const {
    std::vector<ResourceId> ids;
    ids.reserve(index_.size());
    for (const auto& [id, location] : index_) {
        ids.push_back(id);
    }
    return ids;
}

Result<Resource> ResourceManager::load(ResourceId id) const {
    const auto it = index_.find(id);
    if (it == index_.end()) {
        return make_error(ErrorCode::NotFound,
                          std::format("{}.{} does not exist", resource_type_name(id.type), id.number));
    }
    const Location& location = it->second;
    if (location.patch) {
        const std::vector<std::uint8_t>& bytes = files_[location.file].bytes;
        return Resource{
            .id = id,
            .loaded_id = id,
            .data = std::vector<std::uint8_t>(bytes.begin() + location.offset, bytes.end()),
            .source = files_[location.file].name,
        };
    }
    return load_from_volume(id, location);
}

Result<Resource> ResourceManager::load_from_volume(ResourceId id, const Location& location) const {
    const GameFile& volume = files_[location.file];
    const auto header = read_volume_entry_header(volume.bytes, location.offset);
    if (!header) {
        return std::unexpected(header.error());
    }
    const auto compression = sci0_compression(header->method);
    if (!compression) {
        return std::unexpected(compression.error());
    }
    const std::span<const std::uint8_t> packed =
        std::span<const std::uint8_t>(volume.bytes).subspan(location.offset + volume_entry_header_size);
    auto data = decompress(*compression, packed, header->packed_size, header->unpacked_size);
    if (!data) {
        return make_error(data.error().code, std::format("{}.{} in {}: {}", resource_type_name(id.type), id.number,
                                                         volume.name, data.error().message));
    }
    return Resource{
        .id = id,
        .loaded_id = header->id,
        .data = std::move(*data),
        .source = volume.name,
    };
}

} // namespace sci::resource
