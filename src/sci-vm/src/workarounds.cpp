// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/workarounds.cpp
// and translated into this project's style, types and error handling. Only
// the entries for this game (GID_LAURABOW) and for all games are kept.
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

#include "workarounds.hpp"

#include <array>

namespace sci::vm {

namespace {

constexpr WorkaroundSolution fake(std::uint16_t value) {
    return {.type = WorkaroundType::Fake, .value = value};
}

// The original restore dialog (all SCI16 games).
constexpr std::array uninitialized_read_for_param = {
    WorkaroundEntry{
        .room = -1,
        .script = 990,
        .inheritance_level = -1,
        .object = "Restore",
        .method = "doit",
        .from_index = 1,
        .to_index = 1,
        .solution = fake(0),
    },
};

constexpr std::array uninitialized_read = {
    // Pressing Restore in the original restore dialog with an empty list.
    WorkaroundEntry{
        .room = -1,
        .script = 990,
        .inheritance_level = 0,
        .object = "Restore",
        .method = "doit",
        .from_index = 364,
        .to_index = 364,
        .solution = fake(0),
    },
    // Going up the stairs (ScummVM bug #5084).
    WorkaroundEntry{
        .room = 37,
        .script = 0,
        .inheritance_level = 0,
        .object = "CB1",
        .method = "doit",
        .from_index = 1,
        .to_index = 1,
        .solution = fake(0),
    },
    // Any portrait conversation coming up (ScummVM bug #4971).
    WorkaroundEntry{
        .room = -1,
        .script = 967,
        .inheritance_level = 0,
        .object = "myIcon",
        .method = "cycle",
        .from_index = 1,
        .to_index = 1,
        .solution = fake(0),
    },
};

// The demo, after the will is signed: parameter 0 is an object (ScummVM
// bug #4967).
constexpr std::array kdisposescript = {
    WorkaroundEntry{
        .room = 777,
        .script = 777,
        .inheritance_level = 0,
        .object = "myStab",
        .method = "changeState",
        .from_index = 0,
        .to_index = 0,
        .solution = {.type = WorkaroundType::Ignore, .value = 0},
    },
};

} // namespace

std::span<const WorkaroundEntry> arithmetic_workarounds() {
    return {};
}

std::span<const WorkaroundEntry> uninitialized_read_workarounds() {
    return uninitialized_read;
}

std::span<const WorkaroundEntry> uninitialized_read_for_param_workarounds() {
    return uninitialized_read_for_param;
}

std::span<const WorkaroundEntry> kdisposescript_workarounds() {
    return kdisposescript;
}

} // namespace sci::vm
