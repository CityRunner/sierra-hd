// SPDX-License-Identifier: GPL-3.0-or-later
//
// The kernel functions of SCI0 (engines/sci/engine/k*.cpp in ScummVM), by
// their ScummVM names in snake case. The arguments are on the VM stack;
// args.size() is the argument count.

#pragma once

#include "sci/vm/reg.hpp"

#include <span>

namespace sci::vm {

class Machine;

Reg k_load(Machine& machine, std::span<Reg> args);
Reg k_unload(Machine& machine, std::span<Reg> args);
Reg k_script_id(Machine& machine, std::span<Reg> args);
Reg k_dispose_script(Machine& machine, std::span<Reg> args);
Reg k_clone(Machine& machine, std::span<Reg> args);
Reg k_dispose_clone(Machine& machine, std::span<Reg> args);
Reg k_is_object(Machine& machine, std::span<Reg> args);
Reg k_responds_to(Machine& machine, std::span<Reg> args);
Reg k_draw_pic(Machine& machine, std::span<Reg> args);
Reg k_show(Machine& machine, std::span<Reg> args);
Reg k_pic_not_valid(Machine& machine, std::span<Reg> args);
Reg k_animate(Machine& machine, std::span<Reg> args);
Reg k_set_now_seen(Machine& machine, std::span<Reg> args);
Reg k_num_loops(Machine& machine, std::span<Reg> args);
Reg k_num_cels(Machine& machine, std::span<Reg> args);
Reg k_cel_wide(Machine& machine, std::span<Reg> args);
Reg k_cel_high(Machine& machine, std::span<Reg> args);
Reg k_draw_cel(Machine& machine, std::span<Reg> args);
Reg k_add_to_pic(Machine& machine, std::span<Reg> args);
Reg k_new_window(Machine& machine, std::span<Reg> args);
Reg k_get_port(Machine& machine, std::span<Reg> args);
Reg k_set_port(Machine& machine, std::span<Reg> args);
Reg k_dispose_window(Machine& machine, std::span<Reg> args);
Reg k_draw_control(Machine& machine, std::span<Reg> args);
Reg k_hilite_control(Machine& machine, std::span<Reg> args);
Reg k_edit_control(Machine& machine, std::span<Reg> args);
Reg k_text_size(Machine& machine, std::span<Reg> args);
Reg k_display(Machine& machine, std::span<Reg> args);
Reg k_get_event(Machine& machine, std::span<Reg> args);
Reg k_global_to_local(Machine& machine, std::span<Reg> args);
Reg k_local_to_global(Machine& machine, std::span<Reg> args);
Reg k_map_key_to_dir(Machine& machine, std::span<Reg> args);
Reg k_draw_menu_bar(Machine& machine, std::span<Reg> args);
Reg k_menu_select(Machine& machine, std::span<Reg> args);
Reg k_add_menu(Machine& machine, std::span<Reg> args);
Reg k_draw_status(Machine& machine, std::span<Reg> args);
Reg k_parse(Machine& machine, std::span<Reg> args);
Reg k_said(Machine& machine, std::span<Reg> args);
Reg k_set_synonyms(Machine& machine, std::span<Reg> args);
Reg k_have_mouse(Machine& machine, std::span<Reg> args);
Reg k_set_cursor(Machine& machine, std::span<Reg> args);
Reg k_file_io_open(Machine& machine, std::span<Reg> args);
Reg k_file_io_write_string(Machine& machine, std::span<Reg> args);
Reg k_file_io_read_string(Machine& machine, std::span<Reg> args);
Reg k_file_io_close(Machine& machine, std::span<Reg> args);
Reg k_save_game(Machine& machine, std::span<Reg> args);
Reg k_restore_game(Machine& machine, std::span<Reg> args);
Reg k_restart_game(Machine& machine, std::span<Reg> args);
Reg k_game_is_restarting(Machine& machine, std::span<Reg> args);
Reg k_do_sound(Machine& machine, std::span<Reg> args);
Reg k_new_list(Machine& machine, std::span<Reg> args);
Reg k_dispose_list(Machine& machine, std::span<Reg> args);
Reg k_new_node(Machine& machine, std::span<Reg> args);
Reg k_first_node(Machine& machine, std::span<Reg> args);
Reg k_last_node(Machine& machine, std::span<Reg> args);
Reg k_empty_list(Machine& machine, std::span<Reg> args);
Reg k_next_node(Machine& machine, std::span<Reg> args);
Reg k_prev_node(Machine& machine, std::span<Reg> args);
Reg k_node_value(Machine& machine, std::span<Reg> args);
Reg k_add_after(Machine& machine, std::span<Reg> args);
Reg k_add_to_front(Machine& machine, std::span<Reg> args);
Reg k_add_to_end(Machine& machine, std::span<Reg> args);
Reg k_find_key(Machine& machine, std::span<Reg> args);
Reg k_delete_key(Machine& machine, std::span<Reg> args);
Reg k_random(Machine& machine, std::span<Reg> args);
Reg k_abs(Machine& machine, std::span<Reg> args);
Reg k_sqrt(Machine& machine, std::span<Reg> args);
Reg k_get_angle(Machine& machine, std::span<Reg> args);
Reg k_get_distance(Machine& machine, std::span<Reg> args);
Reg k_wait(Machine& machine, std::span<Reg> args);
Reg k_get_time(Machine& machine, std::span<Reg> args);
Reg k_str_end(Machine& machine, std::span<Reg> args);
Reg k_str_cat(Machine& machine, std::span<Reg> args);
Reg k_str_cmp(Machine& machine, std::span<Reg> args);
Reg k_str_len(Machine& machine, std::span<Reg> args);
Reg k_str_cpy(Machine& machine, std::span<Reg> args);
Reg k_format(Machine& machine, std::span<Reg> args);
Reg k_get_far_text(Machine& machine, std::span<Reg> args);
Reg k_read_number(Machine& machine, std::span<Reg> args);
Reg k_base_setter(Machine& machine, std::span<Reg> args);
Reg k_dir_loop(Machine& machine, std::span<Reg> args);
Reg k_can_be_here(Machine& machine, std::span<Reg> args);
Reg k_on_control(Machine& machine, std::span<Reg> args);
Reg k_init_bresen(Machine& machine, std::span<Reg> args);
Reg k_do_bresen(Machine& machine, std::span<Reg> args);
Reg k_do_avoider(Machine& machine, std::span<Reg> args);
Reg k_set_jump(Machine& machine, std::span<Reg> args);
Reg k_set_debug(Machine& machine, std::span<Reg> args);
Reg k_dummy(Machine& machine, std::span<Reg> args);
Reg k_memory_info(Machine& machine, std::span<Reg> args);
Reg k_get_menu(Machine& machine, std::span<Reg> args);
Reg k_set_menu(Machine& machine, std::span<Reg> args);
Reg k_get_save_files(Machine& machine, std::span<Reg> args);
Reg k_get_cwd(Machine& machine, std::span<Reg> args);
Reg k_check_free_space(Machine& machine, std::span<Reg> args);
Reg k_valid_path(Machine& machine, std::span<Reg> args);
Reg k_coord_pri(Machine& machine, std::span<Reg> args);
Reg k_str_at(Machine& machine, std::span<Reg> args);
Reg k_device_info(Machine& machine, std::span<Reg> args);
Reg k_get_save_dir(Machine& machine, std::span<Reg> args);
Reg k_check_save_game(Machine& machine, std::span<Reg> args);
Reg k_shake_screen(Machine& machine, std::span<Reg> args);
Reg k_flush_resources(Machine& machine, std::span<Reg> args);
Reg k_times_sin(Machine& machine, std::span<Reg> args);
Reg k_times_cos(Machine& machine, std::span<Reg> args);
Reg k_sin_div(Machine& machine, std::span<Reg> args);
Reg k_cos_div(Machine& machine, std::span<Reg> args);
Reg k_graph(Machine& machine, std::span<Reg> args);
Reg k_joystick(Machine& machine, std::span<Reg> args);
Reg k_do_sound_init(Machine& machine, std::span<Reg> args);
Reg k_do_sound_play(Machine& machine, std::span<Reg> args);
Reg k_empty(Machine& machine, std::span<Reg> args);
Reg k_do_sound_dispose(Machine& machine, std::span<Reg> args);
Reg k_do_sound_mute(Machine& machine, std::span<Reg> args);
Reg k_do_sound_stop(Machine& machine, std::span<Reg> args);
Reg k_do_sound_pause(Machine& machine, std::span<Reg> args);
Reg k_do_sound_resume_after_restore(Machine& machine, std::span<Reg> args);
Reg k_do_sound_master_volume(Machine& machine, std::span<Reg> args);
Reg k_do_sound_update(Machine& machine, std::span<Reg> args);
Reg k_do_sound_fade(Machine& machine, std::span<Reg> args);
Reg k_do_sound_get_polyphony(Machine& machine, std::span<Reg> args);
Reg k_do_sound_stop_all(Machine& machine, std::span<Reg> args);
Reg k_graph_get_color_count(Machine& machine, std::span<Reg> args);
Reg k_graph_draw_line(Machine& machine, std::span<Reg> args);
Reg k_graph_save_box(Machine& machine, std::span<Reg> args);
Reg k_graph_restore_box(Machine& machine, std::span<Reg> args);
Reg k_graph_fill_box_background(Machine& machine, std::span<Reg> args);
Reg k_graph_fill_box_foreground(Machine& machine, std::span<Reg> args);
Reg k_graph_fill_box_any(Machine& machine, std::span<Reg> args);
Reg k_graph_update_box(Machine& machine, std::span<Reg> args);
Reg k_graph_redraw_box(Machine& machine, std::span<Reg> args);
Reg k_graph_adjust_priority(Machine& machine, std::span<Reg> args);

} // namespace sci::vm
