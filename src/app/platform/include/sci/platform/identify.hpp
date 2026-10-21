// SPDX-License-Identifier: GPL-3.0-or-later
//
// Identifies the game folder by its resource hashes (spec section 10.2 and
// docs/ASSUMPTIONS.md, "Accepted game releases").
#pragma once

#include "sci/core/error.hpp"
#include "sci/resource/resource_manager.hpp"

#include <span>
#include <string>
#include <vector>

namespace sci::platform {

enum class Release : std::int8_t {
    Tested,    // The tested English DOS 3.5" release (golden hashes and hints are made with it).
    Floppy525, // The English DOS 5.25" release, recognised by ScummVM's detection hashes.
};

struct Identification {
    Release release = Release::Tested;
    std::string name;
    std::vector<std::string> warnings; // To show to the user.
};

// The release of the game in the files, or an error that says why the
// folder is not accepted.
[[nodiscard]] core::Result<Identification> identify_game(std::span<const resource::GameFile> files);

} // namespace sci::platform
