// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kscripts.cpp (SCI0 functions)
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

#include "kernel_functions.hpp"
#include "machine.hpp"

#include <format>

namespace sci::vm {

namespace {

// ResourceManager::convertResType for SCI0: the type number in the low 7
// bits, in the order of our ResourceType.
int convert_resource_type(std::uint16_t type) {
    const int number = static_cast<int>(type & 0x7FU);
    return number <= static_cast<int>(ResourceType::Rave) ? number : static_cast<int>(ResourceType::Invalid);
}

constexpr int resource_type_memory = static_cast<int>(ResourceType::Memory);

} // namespace

Reg k_load(Machine& machine, std::span<Reg> args) {
    const int type = convert_resource_type(args[0].to_uint16());
    const int number = args[1].to_uint16();
    // A request for hunk memory.
    if (type == resource_type_memory) {
        return machine.segments().allocate_hunk_entry("kLoad()", static_cast<std::size_t>(number));
    }
    // The resource identifier as handle.
    return vm::number(static_cast<int>((static_cast<unsigned>(type) << 11U) | static_cast<unsigned>(number)));
}

Reg k_unload(Machine& machine, std::span<Reg> args) {
    if (convert_resource_type(args[0].to_uint16()) == resource_type_memory) {
        machine.segments().free_hunk_entry(args[1]);
    }
    return machine.acc();
}

Reg k_clone(Machine& machine, std::span<Reg> args) {
    SegManager& segments = machine.segments();
    const Reg parent_address = args[0];
    const Object* parent = segments.object(parent_address);
    if (parent == nullptr) {
        machine.faults().raise(std::format("Attempt to clone non-object/class at {:04x}:{:04x} failed",
                                           parent_address.segment, parent_address.offset));
        return null_reg;
    }
    std::uint16_t info = parent->info().to_uint16();
    Reg clone_address;
    Object* clone = segments.allocate_clone(clone_address);
    // The allocation may have moved clones; look the parent up again.
    parent = segments.object(parent_address);
    *clone = *parent;
    // Mark as clone.
    info &= static_cast<std::uint16_t>(~info_flag_class);
    clone->set_info(number(info | info_flag_clone));
    clone->set_species(clone->pos());
    if (parent->is_class()) {
        clone->set_super_class(parent->pos());
    }
    if (Script* script = segments.script(parent->pos().segment)) {
        script->increment_lockers();
    }
    if (Script* script = segments.script(clone->pos().segment)) {
        script->increment_lockers();
    }
    return clone_address;
}

Reg k_dispose_clone(Machine& machine, std::span<Reg> args) {
    Object* object = machine.segments().object(args[0]);
    if (object == nullptr) {
        machine.faults().raise(
            std::format("Attempt to dispose non-class/object at {:04x}:{:04x}", args[0].segment, args[0].offset));
        return machine.acc();
    }
    // Only real clones are freed: scripts may set the clone bit on other
    // objects (kq4early clones Sound and then sets bit 1 itself).
    if ((object->info().to_uint16() & 3U) == info_flag_clone) {
        object->mark_as_freed();
    }
    return machine.acc();
}

Reg k_script_id(Machine& machine, std::span<Reg> args) {
    const int script = args[0].to_uint16();
    const int index = args.size() > 1 ? args[1].to_uint16() : 0;
    if (args[0].segment != 0) {
        return args[0];
    }
    SegManager& segments = machine.segments();
    const SegmentId segment = segments.script_segment(script, ScriptLoad::Load);
    if (segment == 0) {
        return null_reg;
    }
    const Script* loaded = segments.script(segment);
    if (loaded == nullptr) {
        return null_reg;
    }
    if (loaded->exports_count() == 0) {
        // Some scripts have no exports; loading them is the point.
        if (args.size() == 2) {
            machine.faults().raise(std::format("Script 0x{:x} does not have a dispatch table and export {} was "
                                               "requested from it",
                                               script, index));
        }
        return null_reg;
    }
    const auto address = loaded->export_offset(index);
    if (!address) {
        machine.faults().raise(std::format("Invalid export {} function pointer in script {}", index, script));
        return null_reg;
    }
    return make_reg(segment, static_cast<int>(*address));
}

Reg k_dispose_script(Machine& machine, std::span<Reg> args) {
    const int script = args[0].offset;
    SegManager& segments = machine.segments();
    const SegmentId id = segments.script_segment(script);
    Script* loaded = segments.script_if_loaded(id);
    if (loaded != nullptr && !loaded->is_marked_deleted()) {
        // The kernel call's own frame is on top; the caller's below it.
        const auto& frames = machine.execution_stack();
        if (!frames.empty() && frames.back().pc.segment != id) {
            loaded->set_lockers(1);
        }
    }
    segments.uninstantiate_script(script);
    return args.size() != 2 ? machine.acc() : args[1];
}

Reg k_is_object(Machine& machine, std::span<Reg> args) {
    // SIGNAL_OFFSET is treated specially.
    if (args[0].offset == 0xFFFF) {
        return null_reg;
    }
    return number(machine.segments().is_heap_object(args[0]) ? 1 : 0);
}

Reg k_responds_to(Machine& machine, std::span<Reg> args) {
    const Reg object = args[0];
    const Selector selector = args[1].to_uint16();
    const bool responds =
        machine.segments().is_heap_object(object) &&
        machine.lookup_selector(object, selector, nullptr, nullptr, nullptr) != Machine::SelectorKind::None;
    return number(responds ? 1 : 0);
}

} // namespace sci::vm
