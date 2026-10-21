// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/object.h, engines/sci/engine/object.cpp (SCI0 paths)
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

#include <cstdint>
#include <vector>

namespace sci::vm {

class SegManager;
class Script;

inline constexpr std::uint16_t info_flag_clone = 0x0001;
inline constexpr std::uint16_t info_flag_class = 0x8000;

// An object or class of a script, or a clone. In SCI0 the first four
// properties are -species-, -super-, -info- and name.
class Object {
public:
    // Reads the object at `pos` of its script: its property values (unless
    // `init_variables` is false), the selectors of a class's properties and
    // its methods.
    void init(const Script& owner, Reg pos, bool init_variables = true);

    [[nodiscard]] Reg species() const { return variables_[0]; }

    void set_species(Reg value) { variables_[0] = value; }

    [[nodiscard]] Reg super_class() const { return variables_[1]; }

    void set_super_class(Reg value) { variables_[1] = value; }

    [[nodiscard]] Reg info() const { return variables_[2]; }

    void set_info(Reg value) { variables_[2] = value; }

    // The static name the compiler gave the object (null if none).
    [[nodiscard]] Reg name() const { return name_; }

    [[nodiscard]] Reg function(std::uint16_t index) const {
        return make_reg(pos_.segment, static_cast<int>(base_method_[(index * 2U) + 1]));
    }

    [[nodiscard]] Selector func_selector(std::uint16_t index) const {
        return static_cast<Selector>(base_method_[std::size_t{index} * 2]);
    }

    // The index of the method the object itself defines for the selector,
    // or -1 (superclasses are not searched).
    [[nodiscard]] int func_selector_position(Selector selector) const;
    // The index of the property for the selector, from the class, or -1.
    [[nodiscard]] int locate_var_selector(const SegManager& segments, Selector selector) const;

    [[nodiscard]] bool is_class() const { return (info().offset & info_flag_class) != 0; }

    [[nodiscard]] const Object* get_class(const SegManager& segments) const;

    void mark_as_freed() { freed_ = true; }

    [[nodiscard]] bool is_freed() const { return freed_; }

    [[nodiscard]] std::size_t var_count() const { return variables_.size(); }

    [[nodiscard]] Reg variable(std::size_t index) const { return variables_[index]; }

    [[nodiscard]] Reg& variable_ref(std::size_t index) { return variables_[index]; }

    [[nodiscard]] std::uint16_t method_count() const { return method_count_; }

    [[nodiscard]] Reg pos() const { return pos_; }

    void set_pos(Reg pos) { pos_ = pos; }

    // Relocates the property at script offset `location` to `segment`.
    bool relocate(SegmentId segment, int location, int heap_offset);

    void init_species(SegManager& segments, Reg address);
    void init_super_class(SegManager& segments, Reg address);
    // Takes the property count from the species class. False if there is
    // no species class.
    bool init_base_object(SegManager& segments, Reg address, bool init_super_class = true);

private:
    Reg name_{};
    std::vector<std::uint16_t> base_vars_;   // Property selectors (classes only).
    std::vector<std::uint32_t> base_method_; // Pairs of method selector and code offset.
    std::vector<Reg> variables_;
    std::uint16_t method_count_ = 0;
    bool freed_ = false;
    Reg pos_{}; // The object's position in its script; for clones, the position of their base.
};

} // namespace sci::vm
