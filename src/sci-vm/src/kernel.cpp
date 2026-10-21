// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kernel.cpp (parseKernelSignature, findRegType,
//   signatureMatch, mapFunctions, loadKernelNames),
//   engines/sci/engine/kernel_tables.h (the SCI0 late, DOS entries)
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

#include "kernel.hpp"

#include "kernel_functions.hpp"
#include "script.hpp"
#include "seg_manager.hpp"

#include <array>
#include <string_view>

namespace sci::vm {

namespace {

enum class SubTable : std::uint8_t { None, DoSound, Graph };

struct Entry {
    const char* name;
    KernelFunctionPointer function;
    const char* signature;
    std::span<const WorkaroundEntry> workarounds;
    SubTable subfunctions;
};

struct SubEntry {
    std::size_t id;
    const char* name;
    KernelFunctionPointer function;
    const char* signature;
};

// The SCI0 kernel table: ScummVM's default names with the SCI0 file
// functions inserted after SetCursor, DoAvoider at 0x55, cut at 0x72.
std::span<const Entry> sci0_table() {
    static const std::array table = {
        Entry{
            .name = "Load",
            .function = k_load,
            .signature = "ii(i*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x00
        Entry{
            .name = "UnLoad",
            .function = k_unload,
            .signature = "i[ir!]",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x01
        Entry{
            .name = "ScriptID",
            .function = k_script_id,
            .signature = "[io](i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x02
        Entry{
            .name = "DisposeScript",
            .function = k_dispose_script,
            .signature = "i(i*)",
            .workarounds = kdisposescript_workarounds(),
            .subfunctions = SubTable::None,
        }, // 0x03
        Entry{
            .name = "Clone",
            .function = k_clone,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x04
        Entry{
            .name = "DisposeClone",
            .function = k_dispose_clone,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x05
        Entry{
            .name = "IsObject",
            .function = k_is_object,
            .signature = ".",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x06
        Entry{
            .name = "RespondsTo",
            .function = k_responds_to,
            .signature = ".i",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x07
        Entry{
            .name = "DrawPic",
            .function = k_draw_pic,
            .signature = "i(i)(i)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x08
        Entry{
            .name = "Show",
            .function = k_show,
            .signature = "i",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x09
        Entry{
            .name = "PicNotValid",
            .function = k_pic_not_valid,
            .signature = "(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x0a
        Entry{
            .name = "Animate",
            .function = k_animate,
            .signature = "(l0)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x0b
        Entry{
            .name = "SetNowSeen",
            .function = k_set_now_seen,
            .signature = "o(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x0c
        Entry{
            .name = "NumLoops",
            .function = k_num_loops,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x0d
        Entry{
            .name = "NumCels",
            .function = k_num_cels,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x0e
        Entry{
            .name = "CelWide",
            .function = k_cel_wide,
            .signature = "ii(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x0f
        Entry{
            .name = "CelHigh",
            .function = k_cel_high,
            .signature = "ii(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x10
        Entry{
            .name = "DrawCel",
            .function = k_draw_cel,
            .signature = "iiiii(i)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x11
        Entry{
            .name = "AddToPic",
            .function = k_add_to_pic,
            .signature = "[il](iiiiii)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x12
        Entry{
            .name = "NewWindow",
            .function = k_new_window,
            .signature = "iiii[r0]i(i)(i)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x13
        Entry{
            .name = "GetPort",
            .function = k_get_port,
            .signature = "",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x14
        Entry{
            .name = "SetPort",
            .function = k_set_port,
            .signature = "i(iiiii)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x15
        Entry{
            .name = "DisposeWindow",
            .function = k_dispose_window,
            .signature = "i(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x16
        Entry{
            .name = "DrawControl",
            .function = k_draw_control,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x17
        Entry{
            .name = "HiliteControl",
            .function = k_hilite_control,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x18
        Entry{
            .name = "EditControl",
            .function = k_edit_control,
            .signature = "[o0][o0]",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x19
        Entry{
            .name = "TextSize",
            .function = k_text_size,
            .signature = "r[r0]i(i)(r0)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x1a
        Entry{
            .name = "Display",
            .function = k_display,
            .signature = "[ir]([ir!]*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x1b
        Entry{
            .name = "GetEvent",
            .function = k_get_event,
            .signature = "io",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x1c
        Entry{
            .name = "GlobalToLocal",
            .function = k_global_to_local,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x1d
        Entry{
            .name = "LocalToGlobal",
            .function = k_local_to_global,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x1e
        Entry{
            .name = "MapKeyToDir",
            .function = k_map_key_to_dir,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x1f
        Entry{
            .name = "DrawMenuBar",
            .function = k_draw_menu_bar,
            .signature = "i",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x20
        Entry{
            .name = "MenuSelect",
            .function = k_menu_select,
            .signature = "o(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x21
        Entry{
            .name = "AddMenu",
            .function = k_add_menu,
            .signature = "rr",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x22
        Entry{
            .name = "DrawStatus",
            .function = k_draw_status,
            .signature = "[r0](i)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x23
        Entry{
            .name = "Parse",
            .function = k_parse,
            .signature = "ro",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x24
        Entry{
            .name = "Said",
            .function = k_said,
            .signature = "[r0]",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x25
        Entry{
            .name = "SetSynonyms",
            .function = k_set_synonyms,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x26
        Entry{
            .name = "HaveMouse",
            .function = k_have_mouse,
            .signature = "",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x27
        Entry{
            .name = "SetCursor",
            .function = k_set_cursor,
            .signature = "i(i)(i)(i)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x28
        Entry{
            .name = "FOpen",
            .function = k_file_io_open,
            .signature = "ri",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x29
        Entry{
            .name = "FPuts",
            .function = k_file_io_write_string,
            .signature = "ir",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x2a
        Entry{
            .name = "FGets",
            .function = k_file_io_read_string,
            .signature = "rii",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x2b
        Entry{
            .name = "FClose",
            .function = k_file_io_close,
            .signature = "i",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x2c
        Entry{
            .name = "SaveGame",
            .function = k_save_game,
            .signature = "[r0]i[r0](r0)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x2d
        Entry{
            .name = "RestoreGame",
            .function = k_restore_game,
            .signature = "[r0]i[r0]",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x2e
        Entry{
            .name = "RestartGame",
            .function = k_restart_game,
            .signature = "",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x2f
        Entry{
            .name = "GameIsRestarting",
            .function = k_game_is_restarting,
            .signature = "(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x30
        Entry{
            .name = "DoSound",
            .function = k_do_sound,
            .signature = "i(.*)",
            .workarounds = {},
            .subfunctions = SubTable::DoSound,
        }, // 0x31
        Entry{
            .name = "NewList",
            .function = k_new_list,
            .signature = "",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x32
        Entry{
            .name = "DisposeList",
            .function = k_dispose_list,
            .signature = "l",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x33
        Entry{
            .name = "NewNode",
            .function = k_new_node,
            .signature = "..",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x34
        Entry{
            .name = "FirstNode",
            .function = k_first_node,
            .signature = "[l0]",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x35
        Entry{
            .name = "LastNode",
            .function = k_last_node,
            .signature = "l",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x36
        Entry{
            .name = "EmptyList",
            .function = k_empty_list,
            .signature = "l",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x37
        Entry{
            .name = "NextNode",
            .function = k_next_node,
            .signature = "n",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x38
        Entry{
            .name = "PrevNode",
            .function = k_prev_node,
            .signature = "n",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x39
        Entry{
            .name = "NodeValue",
            .function = k_node_value,
            .signature = "[n0]",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x3a
        Entry{
            .name = "AddAfter",
            .function = k_add_after,
            .signature = "lnn",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x3b
        Entry{
            .name = "AddToFront",
            .function = k_add_to_front,
            .signature = "ln",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x3c
        Entry{
            .name = "AddToEnd",
            .function = k_add_to_end,
            .signature = "ln",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x3d
        Entry{
            .name = "FindKey",
            .function = k_find_key,
            .signature = "l.",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x3e
        Entry{
            .name = "DeleteKey",
            .function = k_delete_key,
            .signature = "l.",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x3f
        Entry{
            .name = "Random",
            .function = k_random,
            .signature = "(i)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x40
        Entry{
            .name = "Abs",
            .function = k_abs,
            .signature = "i",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x41
        Entry{
            .name = "Sqrt",
            .function = k_sqrt,
            .signature = "i",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x42
        Entry{
            .name = "GetAngle",
            .function = k_get_angle,
            .signature = "iiii",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x43
        Entry{
            .name = "GetDistance",
            .function = k_get_distance,
            .signature = "ii(i)(i)(i)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x44
        Entry{
            .name = "Wait",
            .function = k_wait,
            .signature = "i",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x45
        Entry{
            .name = "GetTime",
            .function = k_get_time,
            .signature = "(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x46
        Entry{
            .name = "StrEnd",
            .function = k_str_end,
            .signature = "r",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x47
        Entry{
            .name = "StrCat",
            .function = k_str_cat,
            .signature = "rr",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x48
        Entry{
            .name = "StrCmp",
            .function = k_str_cmp,
            .signature = "rr(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x49
        Entry{
            .name = "StrLen",
            .function = k_str_len,
            .signature = "[r0]",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x4a
        Entry{
            .name = "StrCpy",
            .function = k_str_cpy,
            .signature = "r[r0](i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x4b
        Entry{
            .name = "Format",
            .function = k_format,
            .signature = "r[ri](.*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x4c
        Entry{
            .name = "GetFarText",
            .function = k_get_far_text,
            .signature = "ii[r0]",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x4d
        Entry{
            .name = "ReadNumber",
            .function = k_read_number,
            .signature = "r",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x4e
        Entry{
            .name = "BaseSetter",
            .function = k_base_setter,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x4f
        Entry{
            .name = "DirLoop",
            .function = k_dir_loop,
            .signature = "oi",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x50
        Entry{
            .name = "CanBeHere",
            .function = k_can_be_here,
            .signature = "o(l)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x51
        Entry{
            .name = "OnControl",
            .function = k_on_control,
            .signature = "ii(i)(i)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x52
        Entry{
            .name = "InitBresen",
            .function = k_init_bresen,
            .signature = "o(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x53
        Entry{
            .name = "DoBresen",
            .function = k_do_bresen,
            .signature = "o",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x54
        Entry{
            .name = "DoAvoider",
            .function = k_do_avoider,
            .signature = "o(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x55
        Entry{
            .name = "SetJump",
            .function = k_set_jump,
            .signature = "oiii",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x56
        Entry{
            .name = "SetDebug",
            .function = k_set_debug,
            .signature = "(i*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x57
        Entry{
            .name = "InspectObj",
            .function = k_dummy,
            .signature = "(.*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x58
        Entry{
            .name = "ShowSends",
            .function = k_dummy,
            .signature = "(.*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x59
        Entry{
            .name = "ShowObjs",
            .function = k_dummy,
            .signature = "(.*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x5a
        Entry{
            .name = "ShowFree",
            .function = k_dummy,
            .signature = "(.*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x5b
        Entry{
            .name = "MemoryInfo",
            .function = k_memory_info,
            .signature = "i",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x5c
        Entry{
            .name = "StackUsage",
            .function = k_dummy,
            .signature = "(.*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x5d
        Entry{
            .name = "Profiler",
            .function = k_dummy,
            .signature = "(.*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x5e
        Entry{
            .name = "GetMenu",
            .function = k_get_menu,
            .signature = "i.",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x5f
        Entry{
            .name = "SetMenu",
            .function = k_set_menu,
            .signature = "i(.*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x60
        Entry{
            .name = "GetSaveFiles",
            .function = k_get_save_files,
            .signature = "rrr",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x61
        Entry{
            .name = "GetCWD",
            .function = k_get_cwd,
            .signature = "r",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x62
        Entry{
            .name = "CheckFreeSpace",
            .function = k_check_free_space,
            .signature = "r",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x63
        Entry{
            .name = "ValidPath",
            .function = k_valid_path,
            .signature = "r",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x64
        Entry{
            .name = "CoordPri",
            .function = k_coord_pri,
            .signature = "i(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x65
        Entry{
            .name = "StrAt",
            .function = k_str_at,
            .signature = "ri(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x66
        Entry{
            .name = "DeviceInfo",
            .function = k_device_info,
            .signature = "i(r)(r)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x67
        Entry{
            .name = "GetSaveDir",
            .function = k_get_save_dir,
            .signature = "",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x68
        Entry{
            .name = "CheckSaveGame",
            .function = k_check_save_game,
            .signature = ".*",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x69
        Entry{
            .name = "ShakeScreen",
            .function = k_shake_screen,
            .signature = "(i)(i)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x6a
        Entry{
            .name = "FlushResources",
            .function = k_flush_resources,
            .signature = "i",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x6b
        Entry{
            .name = "SinMult",
            .function = k_times_sin,
            .signature = "ii",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x6c
        Entry{
            .name = "CosMult",
            .function = k_times_cos,
            .signature = "ii",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x6d
        Entry{
            .name = "SinDiv",
            .function = k_sin_div,
            .signature = "ii",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x6e
        Entry{
            .name = "CosDiv",
            .function = k_cos_div,
            .signature = "ii",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x6f
        Entry{
            .name = "Graph",
            .function = k_graph,
            .signature = nullptr,
            .workarounds = {},
            .subfunctions = SubTable::Graph,
        }, // 0x70
        Entry{
            .name = "Joystick",
            .function = k_joystick,
            .signature = "i(.*)",
            .workarounds = {},
            .subfunctions = SubTable::None,
        }, // 0x71
    };
    return table;
}

std::span<const SubEntry> sub_table(SubTable table) {
    static const std::array do_sound = {
        SubEntry{.id = 0, .name = "DoSoundInit", .function = k_do_sound_init, .signature = "o"},
        SubEntry{.id = 1, .name = "DoSoundPlay", .function = k_do_sound_play, .signature = "o"},
        SubEntry{.id = 2, .name = "DoSoundRestore", .function = k_empty, .signature = "(o)"},
        SubEntry{.id = 3, .name = "DoSoundDispose", .function = k_do_sound_dispose, .signature = "o"},
        SubEntry{.id = 4, .name = "DoSoundMute", .function = k_do_sound_mute, .signature = "(i)"},
        SubEntry{.id = 5, .name = "DoSoundStop", .function = k_do_sound_stop, .signature = "o"},
        SubEntry{.id = 6, .name = "DoSoundPause", .function = k_do_sound_pause, .signature = "i"},
        SubEntry{
            .id = 7,
            .name = "DoSoundResumeAfterRestore",
            .function = k_do_sound_resume_after_restore,
            .signature = "",
        },
        SubEntry{.id = 8, .name = "DoSoundMasterVolume", .function = k_do_sound_master_volume, .signature = "(i)"},
        SubEntry{.id = 9, .name = "DoSoundUpdate", .function = k_do_sound_update, .signature = "o"},
        SubEntry{.id = 10, .name = "DoSoundFade", .function = k_do_sound_fade, .signature = "[o0]"},
        SubEntry{.id = 11, .name = "DoSoundGetPolyphony", .function = k_do_sound_get_polyphony, .signature = ""},
        SubEntry{.id = 12, .name = "DoSoundStopAll", .function = k_do_sound_stop_all, .signature = ""},
    };
    static const std::array graph = {
        SubEntry{.id = 2, .name = "GraphGetColorCount", .function = k_graph_get_color_count, .signature = ""},
        SubEntry{.id = 4, .name = "GraphDrawLine", .function = k_graph_draw_line, .signature = "iiiii(i)(i)"},
        SubEntry{.id = 7, .name = "GraphSaveBox", .function = k_graph_save_box, .signature = "iiiii"},
        SubEntry{.id = 8, .name = "GraphRestoreBox", .function = k_graph_restore_box, .signature = "[r0!]"},
        SubEntry{
            .id = 9,
            .name = "GraphFillBoxBackground",
            .function = k_graph_fill_box_background,
            .signature = "iiii",
        },
        SubEntry{
            .id = 10,
            .name = "GraphFillBoxForeground",
            .function = k_graph_fill_box_foreground,
            .signature = "iiii",
        },
        SubEntry{.id = 11, .name = "GraphFillBoxAny", .function = k_graph_fill_box_any, .signature = "iiiiii(i)(i)"},
        SubEntry{.id = 12, .name = "GraphUpdateBox", .function = k_graph_update_box, .signature = "iiii(i)"},
        SubEntry{.id = 13, .name = "GraphRedrawBox", .function = k_graph_redraw_box, .signature = "iiii"},
        SubEntry{.id = 14, .name = "GraphAdjustPriority", .function = k_graph_adjust_priority, .signature = "ii"},
    };
    switch (table) {
    case SubTable::DoSound:
        return do_sound;
    case SubTable::Graph:
        return graph;
    case SubTable::None:
        break;
    }
    return {};
}

} // namespace

void Kernel::init(bool cant_be_here) {
    functions_.clear();
    for (const Entry& entry : sci0_table()) {
        KernelFunction function{
            .name = entry.name,
            .function = entry.function,
            .signature = parse_signature(entry.signature),
            .workarounds = entry.workarounds,
            .subfunctions = {},
        };
        for (const SubEntry& sub : sub_table(entry.subfunctions)) {
            if (function.subfunctions.size() <= sub.id) {
                function.subfunctions.resize(sub.id + 1);
            }
            function.subfunctions[sub.id] = KernelSubFunction{
                .name = sub.name,
                .function = sub.function,
                .signature = parse_signature(sub.signature),
                .workarounds = {},
            };
        }
        functions_.push_back(std::move(function));
    }
    // Later SCI versions replaced CanBeHere by CantBeHere.
    if (cant_be_here) {
        functions_[0x51].name = "CantBeHere";
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): ScummVM's parseKernelSignature, kept as one.
Signature Kernel::parse_signature(const char* written) {
    if (written == nullptr) {
        return std::nullopt;
    }
    std::vector<std::uint16_t> result;
    std::uint16_t signature = 0;
    bool either_or = false;
    bool optional = false;
    const std::string_view text(written);
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : '\0';
        if (!either_or) {
            switch (c) {
            case '\0':
            case '[':
            case '(':
            case ')':
            case 'i':
            case 'o':
            case 'r':
            case 'l':
            case 'n':
            case '.':
            case '!':
                if (signature != 0) {
                    if (optional) {
                        signature |= sig_is_optional;
                        if (c != ')') {
                            signature |= sig_needs_more;
                        }
                    }
                    result.push_back(signature);
                    signature = 0;
                }
                break;
            default:
                break;
            }
        }
        switch (c) {
        case '[':
            either_or = true;
            break;
        case ']':
            either_or = false;
            break;
        case '(':
            optional = true;
            break;
        case ')':
            optional = false;
            break;
        case '0':
            signature |= sig_type_null;
            break;
        case 'i':
            signature |= sig_type_integer | sig_type_null;
            break;
        case 'o':
            signature |= sig_type_object;
            break;
        case 'r':
            signature |= sig_type_reference;
            break;
        case 'l':
            signature |= sig_type_list;
            break;
        case 'n':
            signature |= sig_type_node;
            break;
        case '.':
            signature |= sig_maybe_any;
            break;
        case '!':
            signature |= sig_is_invalid;
            break;
        case '*':
            signature |= sig_more_may_follow;
            break;
        default:
            break;
        }
    }
    result.push_back(0);
    return result;
}

std::uint16_t Kernel::find_reg_type(const SegManager& segments, Reg reg) {
    if (reg.segment == 0) {
        return sig_type_integer | (reg.offset != 0 ? 0 : sig_type_null);
    }
    if (reg.segment == uninitialized_segment) {
        return sig_type_uninitialized;
    }
    const Segment* segment = segments.segment(reg.segment);
    if (segment == nullptr) {
        return sig_type_error;
    }
    std::uint16_t result = 0;
    if (!segment->is_valid_offset(reg.offset)) {
        result |= sig_is_invalid;
    }
    switch (segment->type()) {
    case SegmentType::Script: {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): the type is the switch case.
        const auto* script = static_cast<const Script*>(segment);
        if (reg.offset <= script->size() && script->offset_is_object(reg.offset)) {
            result |= script->object_at(reg.offset) != nullptr ? sig_type_object : sig_type_reference;
        } else {
            result |= sig_type_reference;
        }
        break;
    }
    case SegmentType::Clones:
        result |= sig_type_object;
        break;
    case SegmentType::Locals:
    case SegmentType::Stack:
    case SegmentType::DynMem:
    case SegmentType::Hunk:
        result |= sig_type_reference;
        break;
    case SegmentType::Lists:
        result |= sig_type_list;
        break;
    case SegmentType::Nodes:
        result |= sig_type_node;
        break;
    case SegmentType::Invalid:
        result = sig_type_error;
        break;
    }
    return result;
}

bool Kernel::signature_match(const SegManager& segments, const std::vector<std::uint16_t>& signature,
                             std::span<const Reg> args) {
    std::size_t position = 0;
    std::uint16_t next = signature[position];
    std::uint16_t current = next;
    std::size_t arg = 0;
    while (next != 0 && arg < args.size()) {
        current = next;
        const std::uint16_t type = find_reg_type(segments, args[arg]);
        if ((type & sig_is_invalid) != 0 && (current & sig_is_invalid) == 0) {
            return false;
        }
        // An unknown type (a stale pointer) passes where invalid pointers
        // are allowed.
        if (((type & ~sig_is_invalid) & current) == 0 &&
            ((type & ~sig_is_invalid) != sig_type_error || (current & sig_is_invalid) == 0)) {
            return false;
        }
        if ((current & sig_more_may_follow) == 0) {
            ++position;
            next = signature[position];
        } else {
            next |= sig_is_optional;
        }
        ++arg;
    }
    if (arg < args.size()) {
        return false; // Too many arguments.
    }
    if (next == 0) {
        return true;
    }
    if ((current & sig_is_optional) != 0) {
        return (current & sig_needs_more) == 0;
    }
    return (next & sig_is_optional) != 0;
}

} // namespace sci::vm
