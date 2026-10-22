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

} // namespace sci::vm
