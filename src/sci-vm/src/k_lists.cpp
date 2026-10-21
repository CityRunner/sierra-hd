// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/klists.cpp (SCI0 functions)
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

void add_to_front(Machine& machine, Reg list_ref, Reg node_ref) {
    const SegManager& segments = machine.segments();
    List* list = segments.lookup_list(list_ref);
    Node* node = segments.lookup_node(node_ref);
    if (list == nullptr || node == nullptr) {
        machine.faults().raise(
            std::format("Attempt to add non-node ({:04x}:{:04x}) to list", node_ref.segment, node_ref.offset));
        return;
    }
    node->pred = null_reg;
    node->succ = list->first;
    if (list->first.is_null()) {
        list->last = node_ref;
    } else if (Node* old = segments.lookup_node(list->first)) {
        old->pred = node_ref;
    }
    list->first = node_ref;
}

void add_to_end(Machine& machine, Reg list_ref, Reg node_ref) {
    const SegManager& segments = machine.segments();
    List* list = segments.lookup_list(list_ref);
    Node* node = segments.lookup_node(node_ref);
    if (list == nullptr || node == nullptr) {
        machine.faults().raise(
            std::format("Attempt to add non-node ({:04x}:{:04x}) to list", node_ref.segment, node_ref.offset));
        return;
    }
    node->pred = list->last;
    node->succ = null_reg;
    if (list->last.is_null()) {
        list->first = node_ref;
    } else if (Node* old = segments.lookup_node(list->last)) {
        old->succ = node_ref;
    }
    list->last = node_ref;
}

Reg find_key(Machine& machine, Reg list_ref, Reg key) {
    const SegManager& segments = machine.segments();
    const List* list = segments.lookup_list(list_ref);
    if (list == nullptr) {
        return null_reg;
    }
    Reg position = list->first;
    while (!position.is_null()) {
        const Node* node = segments.lookup_node(position);
        if (node == nullptr) {
            return null_reg;
        }
        if (node->key == key) {
            return position;
        }
        position = node->succ;
    }
    return null_reg;
}

} // namespace

Reg k_new_list(Machine& machine, std::span<Reg> /*args*/) {
    Reg list_ref;
    List* list = machine.segments().allocate_list(list_ref);
    list->first = null_reg;
    list->last = null_reg;
    return list_ref;
}

// NOLINTNEXTLINE(misc-const-correctness): the kernel function signature.
Reg k_dispose_list(Machine& machine, std::span<Reg> /*args*/) {
    // The garbage collector frees lists.
    return machine.acc();
}

Reg k_new_node(Machine& machine, std::span<Reg> args) {
    const Reg value = args[0];
    const Reg key = args.size() == 2 ? args[1] : args[0];
    return machine.segments().new_node(value, key);
}

Reg k_first_node(Machine& machine, std::span<Reg> args) {
    if (args[0].is_null()) {
        return null_reg;
    }
    const List* list = machine.segments().lookup_list(args[0]);
    return list != nullptr ? list->first : null_reg;
}

Reg k_last_node(Machine& machine, std::span<Reg> args) {
    if (args[0].is_null()) {
        return null_reg;
    }
    const List* list = machine.segments().lookup_list(args[0]);
    return list != nullptr ? list->last : null_reg;
}

Reg k_empty_list(Machine& machine, std::span<Reg> args) {
    if (args[0].is_null()) {
        return null_reg;
    }
    const List* list = machine.segments().lookup_list(args[0]);
    return number(list != nullptr && list->first.is_null() ? 1 : 0);
}

Reg k_next_node(Machine& machine, std::span<Reg> args) {
    const Node* node = machine.segments().lookup_node(args[0]);
    return node != nullptr ? node->succ : null_reg;
}

Reg k_prev_node(Machine& machine, std::span<Reg> args) {
    const Node* node = machine.segments().lookup_node(args[0]);
    return node != nullptr ? node->pred : null_reg;
}

Reg k_node_value(Machine& machine, std::span<Reg> args) {
    const Node* node = machine.segments().lookup_node(args[0]);
    return node != nullptr ? node->value : null_reg;
}

Reg k_add_to_front(Machine& machine, std::span<Reg> args) {
    add_to_front(machine, args[0], args[1]);
    if (args.size() == 3) {
        if (Node* node = machine.segments().lookup_node(args[1])) {
            node->key = args[2];
        }
    }
    return machine.acc();
}

Reg k_add_to_end(Machine& machine, std::span<Reg> args) {
    add_to_end(machine, args[0], args[1]);
    if (args.size() == 3) {
        if (Node* node = machine.segments().lookup_node(args[1])) {
            node->key = args[2];
        }
    }
    return machine.acc();
}

Reg k_add_after(Machine& machine, std::span<Reg> args) {
    const SegManager& segments = machine.segments();
    List* list = segments.lookup_list(args[0]);
    Node* first = segments.lookup_node(args[1]);
    Node* node = segments.lookup_node(args[2]);
    if (node == nullptr || list == nullptr) {
        machine.faults().raise(std::format("New 'node' {:04x}:{:04x} is not a node", args[2].segment, args[2].offset));
        return null_reg;
    }
    if (args.size() != 3 && args.size() != 4) {
        machine.faults().raise("kAddAfter: Haven't got 3 or 4 arguments, aborting");
        return null_reg;
    }
    if (args.size() == 4) {
        node->key = args[3];
    }
    if (first != nullptr) {
        const Reg old_next = first->succ;
        node->pred = args[1];
        first->succ = args[2];
        node->succ = old_next;
        if (old_next.is_null()) {
            list->last = args[2];
        } else if (Node* next = segments.lookup_node(old_next)) {
            next->pred = args[2];
        }
    } else {
        add_to_front(machine, args[0], args[2]);
    }
    return machine.acc();
}

Reg k_find_key(Machine& machine, std::span<Reg> args) {
    return find_key(machine, args[0], args[1]);
}

Reg k_delete_key(Machine& machine, std::span<Reg> args) {
    const SegManager& segments = machine.segments();
    const Reg position = find_key(machine, args[0], args[1]);
    List* list = segments.lookup_list(args[0]);
    if (position.is_null() || list == nullptr) {
        return null_reg;
    }
    Node* node = segments.lookup_node(position);
    if (node == nullptr) {
        return null_reg;
    }
    if (list->first == position) {
        list->first = node->succ;
    }
    if (list->last == position) {
        list->last = node->pred;
    }
    if (!node->pred.is_null()) {
        if (Node* pred = segments.lookup_node(node->pred)) {
            pred->succ = node->succ;
        }
    }
    if (!node->succ.is_null()) {
        if (Node* succ = segments.lookup_node(node->succ)) {
            succ->pred = node->pred;
        }
    }
    // The scripts may use the node again.
    node->pred = null_reg;
    node->succ = null_reg;
    return number(1);
}

} // namespace sci::vm
