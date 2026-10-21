// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kernel.h, engines/sci/engine/kernel.cpp,
//   engines/sci/engine/kernel_tables.h (the SCI0 late, DOS entries)
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

#include "sci/vm/reg.hpp"
#include "workarounds.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sci::vm {

class Machine;
class SegManager;

// A kernel function: the arguments are on the VM stack.
using KernelFunctionPointer = Reg (*)(Machine& machine, std::span<Reg> args);

// Signature bits (engines/sci/engine/kernel.h).
inline constexpr unsigned sig_type_null = 0x01U;
inline constexpr unsigned sig_type_integer = 0x02U;
inline constexpr unsigned sig_type_uninitialized = 0x04U;
inline constexpr unsigned sig_type_object = 0x08U;
inline constexpr unsigned sig_type_reference = 0x10U;
inline constexpr unsigned sig_type_list = 0x20U;
inline constexpr unsigned sig_type_node = 0x40U;
inline constexpr unsigned sig_type_error = 0x80U;
inline constexpr unsigned sig_is_invalid = 0x100U;
inline constexpr unsigned sig_is_optional = 0x200U;
inline constexpr unsigned sig_needs_more = 0x400U;
inline constexpr unsigned sig_more_may_follow = 0x800U;
inline constexpr unsigned sig_maybe_any =
    sig_type_null | sig_type_integer | sig_type_object | sig_type_reference | sig_type_list | sig_type_node;

// A parsed signature; none means no check.
using Signature = std::optional<std::vector<std::uint16_t>>;

struct KernelSubFunction {
    const char* name = nullptr;
    KernelFunctionPointer function = nullptr;
    Signature signature;
    std::span<const WorkaroundEntry> workarounds;
};

struct KernelFunction {
    const char* name = nullptr;
    KernelFunctionPointer function = nullptr;
    Signature signature;
    std::span<const WorkaroundEntry> workarounds;
    std::vector<KernelSubFunction> subfunctions;
};

class Kernel {
public:
    // Builds the SCI0 table; `cant_be_here` renames CanBeHere when the game
    // has that selector.
    void init(bool cant_be_here);

    [[nodiscard]] std::size_t size() const { return functions_.size(); }

    [[nodiscard]] const KernelFunction& function(std::size_t number) const { return functions_[number]; }

    [[nodiscard]] static Signature parse_signature(const char* written);
    [[nodiscard]] static std::uint16_t find_reg_type(const SegManager& segments, Reg reg);
    [[nodiscard]] static bool signature_match(const SegManager& segments, const std::vector<std::uint16_t>& signature,
                                              std::span<const Reg> args);

private:
    std::vector<KernelFunction> functions_;
};

} // namespace sci::vm
