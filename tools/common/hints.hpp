// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loading the hint files of pictures (spec section 6.7) for the tools.
#pragma once

#include "sci/resource/resource_manager.hpp"
#include "sci/vector/hints.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace sci::tools {

// The hints of each picture, in the order given; empty where a picture has
// no hint file. Files that do not parse, or were made for other PIC data,
// are skipped with a message on stderr. Without a directory there are no
// hints.
class PictureHintSet {
public:
    PictureHintSet() = default;
    PictureHintSet(const PictureHintSet&) = delete;
    PictureHintSet& operator=(const PictureHintSet&) = delete;
    PictureHintSet(PictureHintSet&&) = default; // Keeps the pointers valid.
    PictureHintSet& operator=(PictureHintSet&&) = default;
    ~PictureHintSet() = default;

    PictureHintSet(const std::optional<std::filesystem::path>& directory, const resource::ResourceManager& manager,
                   std::span<const std::uint16_t> numbers);

    // For build_scene: per picture, the hints or null.
    [[nodiscard]] std::span<const vector::PictureHints* const> pointers() const { return pointers_; }

private:
    std::vector<std::optional<vector::PictureHints>> hints_;
    std::vector<const vector::PictureHints*> pointers_;
};

} // namespace sci::tools
