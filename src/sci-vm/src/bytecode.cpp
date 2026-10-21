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

#include "bytecode.hpp"

namespace sci::vm {

namespace {

// Operand formats of the opcodes (engines/sci/engine/kernel_tables.h,
// g_base_opcode_formats; SCI0 needs no adjustment).
enum class Operand : std::uint8_t {
    None,
    Byte,
    SByte,
    Word,
    SWord,
    Variable,  // Byte or word, by the opcode's low bit.
    SVariable, // Signed byte or word.
    Relative,  // Signed byte or word.
    End,
    Invalid,
};

using OperandFormat = std::array<Operand, 3>;

constexpr OperandFormat var1{Operand::Variable};
constexpr OperandFormat svar1{Operand::SVariable};
constexpr OperandFormat rel1{Operand::Relative};

constexpr std::array<OperandFormat, 128> opcode_formats = [] {
    std::array<OperandFormat, 128> f{};
    f[0x17] = rel1;                                                   // bt
    f[0x18] = rel1;                                                   // bnt
    f[0x19] = rel1;                                                   // jmp
    f[0x1a] = svar1;                                                  // ldi
    f[0x1c] = svar1;                                                  // pushi
    f[0x1f] = var1;                                                   // link
    f[0x20] = {Operand::Relative, Operand::Byte};                     // call
    f[0x21] = {Operand::Variable, Operand::Byte};                     // callk
    f[0x22] = {Operand::Variable, Operand::Byte};                     // callb
    f[0x23] = {Operand::Variable, Operand::SVariable, Operand::Byte}; // calle
    f[0x24] = {Operand::End};                                         // ret
    f[0x25] = {Operand::Byte};                                        // send
    f[0x26] = {Operand::Invalid};                                     // info (SCI3)
    f[0x27] = {Operand::Invalid};                                     // superP (SCI3)
    f[0x28] = var1;                                                   // class
    f[0x29] = {Operand::Invalid};                                     // dummy
    f[0x2a] = {Operand::Byte};                                        // self
    f[0x2b] = {Operand::Variable, Operand::Byte};                     // super
    f[0x2c] = svar1;                                                  // rest
    f[0x2d] = {Operand::SVariable, Operand::Variable};                // lea
    f[0x2f] = {Operand::Invalid};                                     // dummy
    for (int op = 0x31; op <= 0x38; ++op) {
        f[static_cast<std::size_t>(op)] = var1; // Properties.
    }
    f[0x39] = rel1;            // lofsa
    f[0x3a] = rel1;            // lofss
    f[0x3f] = {Operand::Word}; // line
    for (std::size_t op = 0x40; op < 0x80; ++op) {
        f[op] = var1; // Variables.
    }
    return f;
}();

constexpr int op_push_self = 0x3e;

} // namespace

Instruction read_instruction(std::span<const std::uint8_t> code, std::uint32_t pc) {
    const auto byte_at = [&](std::uint32_t offset) -> std::uint8_t { return offset < code.size() ? code[offset] : 0; };
    const auto word_at = [&](std::uint32_t offset) -> std::uint16_t {
        return static_cast<std::uint16_t>(static_cast<unsigned>(byte_at(offset)) |
                                          (static_cast<unsigned>(byte_at(offset + 1)) << 8U));
    };
    // A byte as a signed value, as a cast to int8 does.
    const auto signed_byte_at = [&](std::uint32_t offset) -> std::int16_t {
        const int value = byte_at(offset);
        return static_cast<std::int16_t>(value >= 0x80 ? value - 0x100 : value);
    };
    Instruction instruction;
    std::uint32_t offset = pc;
    instruction.ext = byte_at(offset++);
    const std::size_t opcode = instruction.ext >> 1U;
    const bool byte_operands = (instruction.ext & 1U) != 0;
    for (std::size_t i = 0; i < 3; ++i) {
        switch (opcode_formats[opcode][i]) {
        case Operand::Byte:
            instruction.params[i] = byte_at(offset++);
            break;
        case Operand::SByte:
            instruction.params[i] = signed_byte_at(offset++);
            break;
        case Operand::Word:
        case Operand::SWord:
            instruction.params[i] = static_cast<std::int16_t>(word_at(offset));
            offset += 2;
            break;
        case Operand::Variable:
            if (byte_operands) {
                instruction.params[i] = byte_at(offset++);
            } else {
                instruction.params[i] = static_cast<std::int16_t>(word_at(offset));
                offset += 2;
            }
            break;
        case Operand::SVariable:
        case Operand::Relative:
            if (byte_operands) {
                instruction.params[i] = signed_byte_at(offset++);
            } else {
                instruction.params[i] = static_cast<std::int16_t>(word_at(offset));
                offset += 2;
            }
            break;
        case Operand::None:
        case Operand::End:
        case Operand::Invalid:
            break;
        }
    }
    // pushSelf with the low bit set is the debug opcode "file", followed by
    // a zero-terminated file name.
    if (opcode == op_push_self && byte_operands) {
        while (true) {
            const std::uint8_t c = byte_at(offset);
            ++offset;
            if (c == 0 || offset >= code.size()) {
                break;
            }
        }
    }
    instruction.length = offset - pc;
    return instruction;
}

} // namespace sci::vm
