// SPDX-License-Identifier: GPL-3.0-or-later
//
// Resource patch files: loose files in the game folder that replace a
// resource, such as SCRIPT.052. Which files count, and in which order they
// are applied, follows ScummVM's readResourcePatches; their content follows
// its processPatch.

#pragma once

#include "sci/resource/resource_id.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sci::resource {

struct PatchCandidate {
    std::size_t file = 0; // Index into the file names passed in.
    ResourceId id;
};

// The files that name a resource as patch files, in the order ScummVM
// applies them: by resource type, then the SCI0-style names (`view.001`),
// then the SCI1-style names (`1.v56`). Within each group the order is by
// file name; ScummVM's order there depends on the directory listing. A
// later patch for the same resource replaces an earlier one.
[[nodiscard]] std::vector<PatchCandidate> find_patch_candidates(std::span<const std::string> file_names);

// Where the resource data starts in a patch file for `expected`, or nullopt
// if the file is not a valid patch for it (too small, other type, header
// beyond the end). The first byte is the type (with bit 7 set), the second
// the size of an extra header that is skipped.
[[nodiscard]] std::optional<std::size_t> patch_data_offset(std::span<const std::uint8_t> file, ResourceType expected);

} // namespace sci::resource
