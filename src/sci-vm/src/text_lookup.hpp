// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kernel.cpp (Kernel::lookupText)
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

#pragma once

#include "machine.hpp"

#include <format>
#include <string>

namespace sci::vm {

// A string: at a pointer, or string `index` of text resource `address`.
inline std::string lookup_text(Machine& machine, Reg address, int index) {
    if (address.segment != 0) {
        return machine.segments().get_string(address);
    }
    const std::vector<std::uint8_t>* text = machine.resources().find(ResourceType::Text, address.offset);
    if (text == nullptr) {
        machine.faults().raise(std::format("text.{:03} not found", address.offset));
        return {};
    }
    std::size_t position = 0;
    std::size_t remaining = text->size();
    const int requested = index;
    while (index-- > 0 && remaining > 0) {
        while (remaining > 0) {
            --remaining;
            if ((*text)[position++] == 0) {
                break;
            }
        }
    }
    if (remaining > 0) {
        std::string result;
        while (position < text->size() && (*text)[position] != 0) {
            result += static_cast<char>((*text)[position++]);
        }
        return result;
    }
    warn(std::format("Index {} out of bounds in text.{:03}", requested, address.offset));
    return {};
}

} // namespace sci::vm
