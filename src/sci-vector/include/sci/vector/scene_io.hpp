// SPDX-License-Identifier: GPL-3.0-or-later
//
// A versioned binary form of the vector scene, for the analysis cache
// (spec section 10.2; docs/formats/analysis-cache.md). Doubles are stored
// as their IEEE 754 bits, so a scene read back is identical to the one
// written and renders bit for bit the same.
#pragma once

#include "sci/core/error.hpp"
#include "sci/vector/scene.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::vector {

// Bumped when the encoding changes.
inline constexpr std::uint16_t scene_format_version = 1;

[[nodiscard]] std::vector<std::uint8_t> write_scene(const Scene& scene);

// Accepts only a complete, consistent scene of this format version: face
// indices in range, block pixels inside the picture and ascending, finite
// coordinates, nothing after the end.
[[nodiscard]] core::Result<Scene> read_scene(std::span<const std::uint8_t> bytes);

} // namespace sci::vector
