// SPDX-License-Identifier: GPL-3.0-or-later
//
// The hash of the VM state (milestone 8, docs/ASSUMPTIONS.md, "Milestone 8:
// the VM state"): SHA-256 of a canonical serialisation of everything that
// influences the game's future execution, per component and in total.

#pragma once

#include "sci/core/sha256.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace sci::vm {

struct StateComponentHash {
    std::string name;
    core::Sha256Digest digest{};
    std::size_t bytes = 0; // The length of the component's serialisation.
};

struct StateHash {
    // In a fixed order: execution, segments, machine, clock, random,
    // events, files, parser, sound, graphics.
    std::vector<StateComponentHash> components;
    // Of all components.
    core::Sha256Digest total{};
    // Of all components but "execution": the state a save holds (a restore
    // re-enters the game through its replay method, as in SCI).
    core::Sha256Digest persistent{};
};

} // namespace sci::vm
