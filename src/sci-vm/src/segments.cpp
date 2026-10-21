// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/segment.cpp (SCI0 paths)
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

#include "segments.hpp"

#include "seg_manager.hpp"

#include <format>

namespace sci::vm {

namespace {

// A reference to registers from `pointer` (a byte offset) to the end.
SegmentRef register_ref(std::vector<Reg>& regs, Reg pointer) {
    SegmentRef ref;
    ref.is_raw = false;
    const std::size_t index = pointer.offset / 2U;
    ref.max_size = (static_cast<int>(regs.size()) - static_cast<int>(index)) * 2;
    if ((pointer.offset & 1U) != 0) {
        ref.max_size -= 1;
        ref.skip_byte = true;
    }
    if (ref.max_size > 0) {
        ref.regs = std::span<Reg>(regs).subspan(index);
    }
    return ref;
}

} // namespace

SegmentRef Segment::dereference(Reg /*pointer*/) {
    return {};
}

Reg Segment::find_canonic_address(const SegManager& /*segments*/, Reg address) const {
    return address;
}

void Segment::free_at_address(SegManager& /*segments*/, Reg /*address*/) {
}

std::vector<Reg> Segment::list_all_deallocatable(SegmentId /*id*/) const {
    return {};
}

std::vector<Reg> Segment::list_all_outgoing_references(Reg /*address*/) const {
    return {};
}

SegmentRef LocalVariables::dereference(Reg pointer) {
    SegmentRef ref = register_ref(locals, pointer);
    if (ref.max_size <= 0) {
        warn(std::format("LocalVariables::dereference: Offset at end or out of bounds {:04x}:{:04x}", pointer.segment,
                         pointer.offset));
    }
    return ref;
}

Reg LocalVariables::find_canonic_address(const SegManager& segments, Reg /*address*/) const {
    return make_reg(segments.script_segment(script_id), 0);
}

std::vector<Reg> LocalVariables::list_all_outgoing_references(Reg /*address*/) const {
    return locals;
}

SegmentRef DataStack::dereference(Reg pointer) {
    return register_ref(entries, pointer);
}

std::vector<Reg> DataStack::list_all_outgoing_references(Reg /*address*/) const {
    return entries;
}

void CloneTable::free_at_address(SegManager& /*segments*/, Reg address) {
    free_entry(address.offset);
}

std::vector<Reg> CloneTable::list_all_outgoing_references(Reg address) const {
    std::vector<Reg> result;
    if (!is_valid_entry(address.offset)) {
        return result;
    }
    const Object& clone = at(address.offset);
    for (std::size_t i = 0; i < clone.var_count(); ++i) {
        result.push_back(clone.variable(i));
    }
    // The base object, which also reaches the script's locals.
    result.push_back(clone.pos());
    return result;
}

std::vector<Reg> ListTable::list_all_outgoing_references(Reg address) const {
    if (!is_valid_entry(address.offset)) {
        return {};
    }
    const List& list = at(address.offset);
    return {list.first, list.last};
}

std::vector<Reg> NodeTable::list_all_outgoing_references(Reg address) const {
    if (!is_valid_entry(address.offset)) {
        return {};
    }
    const Node& node = at(address.offset);
    return {node.pred, node.succ, node.key, node.value};
}

SegmentRef DynMem::dereference(Reg pointer) {
    SegmentRef ref;
    ref.is_raw = true;
    if (pointer.offset > buffer.size()) {
        return ref;
    }
    ref.max_size = static_cast<int>(buffer.size() - pointer.offset);
    ref.raw = std::span<std::uint8_t>(buffer).subspan(pointer.offset);
    return ref;
}

} // namespace sci::vm
