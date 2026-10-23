// SPDX-License-Identifier: GPL-3.0-or-later
//
// Access to the resources of an SCI0 game folder: the resource map, the
// volumes and loose patch files, combined as ScummVM's ResourceManager does.
// The library does no file I/O: the caller passes the folder's files.

#pragma once

#include "sci/core/error.hpp"
#include "sci/resource/resource_id.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sci::resource {

// One file of the game folder: its name as stored (any case) and content.
struct GameFile {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

struct Resource {
    ResourceId id;
    // The id in the volume header, which ScummVM adopts when it loads a
    // resource. Equal to `id` for well-formed data and for patch files.
    ResourceId loaded_id;
    std::vector<std::uint8_t> data;
    std::string source; // Name of the file the data came from.
};

class ResourceManager {
public:
    // Indexes the folder: checks that it holds an SCI0 map and volumes, reads
    // the map (the first entry for an id wins) and applies the patch files.
    // Map entries pointing past the end of their volume are skipped with a
    // warning in the Resources log category, as in ScummVM.
    [[nodiscard]] static core::Result<ResourceManager> open(std::vector<GameFile> files);

    // All resource ids, ordered by type and number.
    [[nodiscard]] std::vector<ResourceId> list() const;

    [[nodiscard]] bool contains(ResourceId id) const { return index_.contains(id); }

    // Reads and decompresses one resource. Resources are not cached here.
    [[nodiscard]] core::Result<Resource> load(ResourceId id) const;

    // A file of the game folder by name, in any case (the AdLib driver reads
    // its bank from ADL.DRV when there is no patch.003); null if missing.
    [[nodiscard]] const GameFile* file(std::string_view name) const;

private:
    struct Location {
        std::size_t file = 0;
        std::uint32_t offset = 0; // Of the volume entry header, or of the data in a patch file.
        bool patch = false;
    };

    struct VolumeFile {
        std::size_t file = 0;
        std::uint8_t number = 0;
    };

    explicit ResourceManager(std::vector<GameFile> files) : files_(std::move(files)) {}

    [[nodiscard]] std::vector<VolumeFile> find_volumes() const;
    core::Result<void> index_map(std::size_t map_file, std::span<const VolumeFile> volumes);
    void apply_patches();
    [[nodiscard]] core::Result<Resource> load_from_volume(ResourceId id, const Location& location) const;

    std::vector<GameFile> files_;
    std::map<ResourceId, Location> index_;
};

} // namespace sci::resource
