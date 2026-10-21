// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/workarounds.h, engines/sci/engine/workarounds.cpp
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

#pragma once

#include <cstdint>
#include <span>

namespace sci::vm {

enum class WorkaroundType : std::uint8_t {
    None,      // No workaround found.
    Ignore,    // Skip the kernel call.
    StillCall, // Make the kernel call anyway.
    Fake,      // Use the value instead (kernel result, temporary, operation).
};

struct WorkaroundSolution {
    WorkaroundType type = WorkaroundType::None;
    std::uint16_t value = 0;
};

// A script bug and its solution, matched by room, script, object and
// method (-1 matches any), and for variables by their index.
struct WorkaroundEntry {
    int room = -1;
    int script = -1;
    int inheritance_level = -1;
    const char* object = nullptr; // Null matches any object.
    const char* method = nullptr;
    int from_index = -1;
    int to_index = -1;
    WorkaroundSolution solution;
};

[[nodiscard]] std::span<const WorkaroundEntry> arithmetic_workarounds();
[[nodiscard]] std::span<const WorkaroundEntry> uninitialized_read_workarounds();
[[nodiscard]] std::span<const WorkaroundEntry> uninitialized_read_for_param_workarounds();
[[nodiscard]] std::span<const WorkaroundEntry> kdisposescript_workarounds();

} // namespace sci::vm
