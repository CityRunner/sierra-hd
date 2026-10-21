// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kernel functions not ported yet: calling one stops the VM with a fault.

#include "kernel_functions.hpp"
#include "machine.hpp"

namespace sci::vm {

namespace {

Reg not_implemented(Machine& machine, const char* name) {
    machine.faults().raise(std::string(name) + " is not implemented");
    return null_reg;
}

} // namespace

Reg k_do_avoider(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_do_avoider");
}

Reg k_draw_control(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_draw_control");
}

Reg k_edit_control(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_edit_control");
}

Reg k_hilite_control(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_hilite_control");
}

Reg k_menu_select(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_menu_select");
}

Reg k_parse(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_parse");
}

Reg k_said(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_said");
}

} // namespace sci::vm
