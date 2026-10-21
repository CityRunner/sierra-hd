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

Reg k_check_save_game(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_check_save_game");
}

Reg k_do_avoider(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_do_avoider");
}

Reg k_draw_control(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_draw_control");
}

Reg k_edit_control(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_edit_control");
}

Reg k_file_io_close(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_file_io_close");
}

Reg k_file_io_open(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_file_io_open");
}

Reg k_file_io_read_string(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_file_io_read_string");
}

Reg k_file_io_write_string(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_file_io_write_string");
}

Reg k_get_save_files(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_get_save_files");
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

Reg k_restore_game(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_restore_game");
}

Reg k_said(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_said");
}

Reg k_save_game(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_save_game");
}

Reg k_set_jump(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_set_jump");
}

} // namespace sci::vm
