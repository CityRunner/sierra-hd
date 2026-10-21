// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/object.cpp (SCI0 paths)
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

#include "object.hpp"

#include "script.hpp"
#include "seg_manager.hpp"

#include <format>
#include <utility>

namespace sci::vm {

namespace {

// The object header before the object's position: the counts and offsets
// of SCI0 (engines/sci/engine/object.h, ObjectOffsets).
constexpr std::uint32_t header_size = 6;
constexpr std::uint32_t header_function_area = 2;
constexpr std::uint32_t header_selector_counter = 4;

} // namespace

void Object::init(const Script& owner, Reg pos, bool init_variables) {
    pos_ = pos;
    const std::uint32_t data = pos.offset;
    const std::uint32_t header = data - header_size;
    variables_.resize(owner.read16(header + header_selector_counter));

    // Non-class objects do not have a table of property selectors.
    const std::uint16_t info_selector = owner.read16(data + (2 * 2));
    base_vars_.clear();
    if ((info_selector & info_flag_class) != 0) {
        base_vars_.reserve(variables_.size());
        std::uint32_t offset = data + static_cast<std::uint32_t>(variables_.size() * 2);
        for (std::size_t i = 0; i < variables_.size(); ++i) {
            base_vars_.push_back(owner.read16(offset));
            offset += 2;
        }
    }

    // The method block: a count, the selectors, a zero, the code offsets.
    const std::uint32_t method_block = data + owner.read16(header + header_function_area) - 2;
    method_count_ = owner.read16(method_block);
    if (owner.read16(method_block + 2 + (method_count_ * 2U)) != 0) {
        warn(std::format("Object {:04x}:{:04x} in script {} has a value in its zero-terminator field", pos.segment,
                         pos.offset, owner.number()));
    }
    base_method_.clear();
    base_method_.reserve(method_count_ * 2U);
    for (std::uint32_t i = 0; i < method_count_; ++i) {
        base_method_.push_back(owner.read16(method_block + 2 + (i * 2)));
        base_method_.push_back(owner.read16(method_block + 2 + ((method_count_ + 1 + i) * 2)));
    }

    // The static name, if the relocation table says the name property is a
    // pointer.
    const std::uint32_t name_offset = data + (3 * 2);
    if (const auto relocation = owner.relocation_offset(name_offset)) {
        name_ = make_reg(pos.segment, static_cast<int>(*relocation + owner.read16(name_offset)));
    }

    if (init_variables) {
        for (std::size_t i = 0; i < variables_.size(); ++i) {
            variables_[i] = number(owner.read16(data + static_cast<std::uint32_t>(i * 2)));
        }
    }
}

int Object::func_selector_position(Selector selector) const {
    for (std::uint16_t i = 0; i < method_count_; ++i) {
        if (func_selector(i) == selector) {
            return i;
        }
    }
    return -1;
}

const Object* Object::get_class(const SegManager& segments) const {
    return is_class() ? this : segments.object(super_class());
}

int Object::locate_var_selector(const SegManager& segments, Selector selector) const {
    const Object* klass = get_class(segments);
    if (klass == nullptr) {
        return -1;
    }
    const std::size_t count = std::min(klass->var_count(), klass->base_vars_.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (std::cmp_equal(klass->base_vars_[i], selector)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Object::relocate(SegmentId segment, int location, int heap_offset) {
    const int relative = location - static_cast<int>(pos_.offset);
    if (relative < 0) {
        return false;
    }
    const auto index = static_cast<std::size_t>(relative / 2);
    if (index >= variables_.size()) {
        return false;
    }
    if (relative % 2 != 0) {
        return false;
    }
    variables_[index].segment = segment;
    variables_[index].offset = static_cast<std::uint16_t>(variables_[index].offset + heap_offset);
    return true;
}

void Object::init_species(SegManager& segments, Reg address) {
    const std::uint16_t species_offset = species().offset;
    if (species_offset == 0xFFFF) {
        set_species(null_reg);
    } else {
        set_species(segments.class_address(species_offset, ScriptLoad::Lock, address.segment));
    }
}

void Object::init_super_class(SegManager& segments, Reg address) {
    const std::uint16_t super_offset = super_class().offset;
    if (super_offset == 0xFFFF) {
        set_super_class(null_reg);
    } else {
        set_super_class(segments.class_address(super_offset, ScriptLoad::Lock, address.segment));
    }
}

bool Object::init_base_object(SegManager& segments, Reg address, bool init_super) {
    const Object* base = segments.object(species());
    if (base == nullptr) {
        return false;
    }
    if (variables_.size() != base->var_count()) {
        debug(std::format("Object {:04x}:{:04x} declares {} properties, its class {}", pos_.segment, pos_.offset,
                          variables_.size(), base->var_count()));
        variables_.resize(base->var_count());
    }
    if (init_super) {
        init_super_class(segments, address);
    }
    return true;
}

} // namespace sci::vm
