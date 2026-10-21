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

Reg k_draw_pic(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_draw_pic");
}

Reg k_show(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_show");
}

Reg k_add_menu(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_add_menu");
}

Reg k_add_to_pic(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_add_to_pic");
}

Reg k_animate(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_animate");
}

Reg k_base_setter(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_base_setter");
}

Reg k_can_be_here(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_can_be_here");
}

Reg k_cel_high(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_cel_high");
}

Reg k_cel_wide(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_cel_wide");
}

Reg k_check_free_space(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_check_free_space");
}

Reg k_check_save_game(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_check_save_game");
}

Reg k_coord_pri(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_coord_pri");
}

Reg k_device_info(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_device_info");
}

Reg k_dir_loop(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_dir_loop");
}

Reg k_display(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_display");
}

Reg k_dispose_window(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_dispose_window");
}

Reg k_do_avoider(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_do_avoider");
}

Reg k_do_bresen(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_do_bresen");
}

Reg k_draw_cel(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_draw_cel");
}

Reg k_draw_control(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_draw_control");
}

Reg k_draw_menu_bar(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_draw_menu_bar");
}

Reg k_draw_status(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_draw_status");
}

Reg k_dummy(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_dummy");
}

Reg k_edit_control(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_edit_control");
}

Reg k_empty(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_empty");
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

Reg k_flush_resources(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_flush_resources");
}

Reg k_game_is_restarting(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_game_is_restarting");
}

Reg k_get_cwd(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_get_cwd");
}

Reg k_get_event(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_get_event");
}

Reg k_get_menu(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_get_menu");
}

Reg k_get_port(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_get_port");
}

Reg k_get_save_dir(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_get_save_dir");
}

Reg k_get_save_files(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_get_save_files");
}

Reg k_get_time(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_get_time");
}

Reg k_global_to_local(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_global_to_local");
}

Reg k_graph(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph");
}

Reg k_graph_adjust_priority(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_adjust_priority");
}

Reg k_graph_draw_line(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_draw_line");
}

Reg k_graph_fill_box_any(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_fill_box_any");
}

Reg k_graph_fill_box_background(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_fill_box_background");
}

Reg k_graph_fill_box_foreground(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_fill_box_foreground");
}

Reg k_graph_get_color_count(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_get_color_count");
}

Reg k_graph_redraw_box(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_redraw_box");
}

Reg k_graph_restore_box(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_restore_box");
}

Reg k_graph_save_box(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_save_box");
}

Reg k_graph_update_box(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_graph_update_box");
}

Reg k_have_mouse(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_have_mouse");
}

Reg k_hilite_control(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_hilite_control");
}

Reg k_init_bresen(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_init_bresen");
}

Reg k_joystick(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_joystick");
}

Reg k_local_to_global(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_local_to_global");
}

Reg k_map_key_to_dir(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_map_key_to_dir");
}

Reg k_memory_info(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_memory_info");
}

Reg k_menu_select(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_menu_select");
}

Reg k_new_window(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_new_window");
}

Reg k_num_cels(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_num_cels");
}

Reg k_num_loops(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_num_loops");
}

Reg k_on_control(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_on_control");
}

Reg k_parse(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_parse");
}

Reg k_pic_not_valid(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_pic_not_valid");
}

Reg k_restart_game(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_restart_game");
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

Reg k_set_cursor(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_set_cursor");
}

Reg k_set_debug(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_set_debug");
}

Reg k_set_jump(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_set_jump");
}

Reg k_set_menu(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_set_menu");
}

Reg k_set_now_seen(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_set_now_seen");
}

Reg k_set_port(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_set_port");
}

Reg k_set_synonyms(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_set_synonyms");
}

Reg k_shake_screen(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_shake_screen");
}

Reg k_text_size(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_text_size");
}

Reg k_valid_path(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_valid_path");
}

Reg k_wait(Machine& machine, std::span<Reg> /*args*/) {
    return not_implemented(machine, "k_wait");
}

} // namespace sci::vm
