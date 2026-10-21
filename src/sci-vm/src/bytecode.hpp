// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/vm.cpp (readPMachineInstruction),
//   engine/kernel_tables.h (g_base_opcode_formats)
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

#include <array>
#include <cstdint>
#include <span>

namespace sci::vm {

// One decoded instruction (readPMachineInstruction).
struct Instruction {
    std::uint8_t ext = 0; // The opcode byte; bit 0 selects byte operands.
    std::array<std::int16_t, 4> params{};
    std::uint32_t length = 0;
};

inline constexpr int op_callk = 0x21;

// `value >> 1` on a signed value, as ScummVM shifts its int16 operands: the
// arithmetic shift rounds towards negative infinity.
constexpr int half_floor(int value) {
    return value >= 0 ? value / 2 : -((1 - value) / 2);
}

// Reads the instruction at `pc`. Bytes past the end of the script read as 0.
Instruction read_instruction(std::span<const std::uint8_t> code, std::uint32_t pc);

} // namespace sci::vm
