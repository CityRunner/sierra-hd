// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/gc.cpp
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

#include "machine.hpp"
#include "subsystems.hpp"

#include <set>

namespace sci::vm {

namespace {

struct RegLess {
    bool operator()(Reg a, Reg b) const { return a.segment != b.segment ? a.segment < b.segment : a.offset < b.offset; }
};

using AddressSet = std::set<Reg, RegLess>;

class Worklist {
public:
    void push(Reg reg) {
        if (reg.segment == 0) {
            return; // Numbers.
        }
        if (seen_.insert(reg).second) {
            work_.push_back(reg);
        }
    }

    void push_all(const std::vector<Reg>& regs) {
        for (const Reg reg : regs) {
            push(reg);
        }
    }

    [[nodiscard]] bool empty() const { return work_.empty(); }

    Reg pop() {
        const Reg reg = work_.back();
        work_.pop_back();
        return reg;
    }

    [[nodiscard]] const AddressSet& seen() const { return seen_; }

private:
    AddressSet seen_;
    std::vector<Reg> work_;
};

} // namespace

// Frees whatever the scripts can no longer reach: unreferenced clones,
// lists, nodes, hunks, dynamic memory, and scripts marked deleted.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): ScummVM's run_gc, kept as one function.
void Machine::run_gc() {
    Worklist work;
    work.push(acc_);
    work.push(prev_);
    // The value stack, up to the stack pointer of the topmost script frame.
    auto top = stack_frames_.rbegin();
    if (top != stack_frames_.rend() && top->type == ExecType::Kernel) {
        ++top;
    }
    if (top != stack_frames_.rend()) {
        for (StackIndex i = 0; i < top->sp && i < stack_->entries.size(); ++i) {
            work.push(stack_->entries[i]);
        }
    }
    // The execution stack.
    for (const ExecStack& frame : stack_frames_) {
        if (frame.type != ExecType::Kernel) {
            work.push(frame.objp);
            work.push(frame.sendp);
            if (frame.type == ExecType::VarSelector) {
                if (const Object* obj = segments_.object(frame.var_object)) {
                    work.push(obj->variable(static_cast<std::size_t>(frame.var_index)));
                }
            }
        }
    }
    // Explicitly loaded scripts.
    for (std::size_t i = 1; i < segments_.heap_size(); ++i) {
        const Script* script = segments_.script_if_loaded(static_cast<SegmentId>(i));
        if (script != nullptr && script->lockers() != 0) {
            work.push_all(script->list_object_references());
        }
    }
    // Everything reachable.
    const SegmentId stack_segment = segments_.find_segment_by_type(SegmentType::Stack);
    while (!work.empty()) {
        const Reg reg = work.pop();
        if (reg.segment != stack_segment) {
            if (const Segment* segment = segments_.segment(reg.segment)) {
                work.push_all(segment->list_all_outgoing_references(reg));
            }
        }
    }
    // Hunks the graphics engine holds (window bits).
    for (const Reg hunk : subsystems_->engine_hunks()) {
        work.push(hunk);
    }
    AddressSet active;
    for (const Reg reg : work.seen()) {
        if (const Segment* segment = segments_.segment(reg.segment)) {
            active.insert(segment->find_canonic_address(segments_, reg));
        }
    }
    // Free what is not active, segment by segment.
    for (std::size_t i = 1; i < segments_.heap_size(); ++i) {
        Segment* segment = segments_.segment(static_cast<SegmentId>(i));
        if (segment == nullptr) {
            continue;
        }
        for (const Reg address : segment->list_all_deallocatable(static_cast<SegmentId>(i))) {
            if (!active.contains(address)) {
                segment->free_at_address(segments_, address);
                // Freeing a script may free the segment itself.
                segment = segments_.segment(static_cast<SegmentId>(i));
                if (segment == nullptr) {
                    break;
                }
            }
        }
    }
}

} // namespace sci::vm
