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
        Entry{"Load", k_load, "ii(i*)", {}, SubTable::None},                                             // 0x00
        Entry{"UnLoad", k_unload, "i[ir!]", {}, SubTable::None},                                         // 0x01
        Entry{"ScriptID", k_script_id, "[io](i)", {}, SubTable::None},                                   // 0x02
        Entry{"DisposeScript", k_dispose_script, "i(i*)", kdisposescript_workarounds(), SubTable::None}, // 0x03
        Entry{"Clone", k_clone, "o", {}, SubTable::None},                                                // 0x04
        Entry{"DisposeClone", k_dispose_clone, "o", {}, SubTable::None},                                 // 0x05
        Entry{"IsObject", k_is_object, ".", {}, SubTable::None},                                         // 0x06
        Entry{"RespondsTo", k_responds_to, ".i", {}, SubTable::None},                                    // 0x07
        Entry{"DrawPic", k_draw_pic, "i(i)(i)(i)", {}, SubTable::None},                                  // 0x08
        Entry{"Show", k_show, "i", {}, SubTable::None},                                                  // 0x09
        Entry{"PicNotValid", k_pic_not_valid, "(i)", {}, SubTable::None},                                // 0x0a
        Entry{"Animate", k_animate, "(l0)(i)", {}, SubTable::None},                                      // 0x0b
        Entry{"SetNowSeen", k_set_now_seen, "o(i)", {}, SubTable::None},                                 // 0x0c
        Entry{"NumLoops", k_num_loops, "o", {}, SubTable::None},                                         // 0x0d
        Entry{"NumCels", k_num_cels, "o", {}, SubTable::None},                                           // 0x0e
        Entry{"CelWide", k_cel_wide, "ii(i)", {}, SubTable::None},                                       // 0x0f
        Entry{"CelHigh", k_cel_high, "ii(i)", {}, SubTable::None},                                       // 0x10
        Entry{"DrawCel", k_draw_cel, "iiiii(i)(i)", {}, SubTable::None},                                 // 0x11
        Entry{"AddToPic", k_add_to_pic, "[il](iiiiii)", {}, SubTable::None},                             // 0x12
        Entry{"NewWindow", k_new_window, "iiii[r0]i(i)(i)(i)", {}, SubTable::None},                      // 0x13
        Entry{"GetPort", k_get_port, "", {}, SubTable::None},                                            // 0x14
        Entry{"SetPort", k_set_port, "i(iiiii)(i)", {}, SubTable::None},                                 // 0x15
        Entry{"DisposeWindow", k_dispose_window, "i(i)", {}, SubTable::None},                            // 0x16
        Entry{"DrawControl", k_draw_control, "o", {}, SubTable::None},                                   // 0x17
        Entry{"HiliteControl", k_hilite_control, "o", {}, SubTable::None},                               // 0x18
        Entry{"EditControl", k_edit_control, "[o0][o0]", {}, SubTable::None},                            // 0x19
        Entry{"TextSize", k_text_size, "r[r0]i(i)(r0)", {}, SubTable::None},                             // 0x1a
        Entry{"Display", k_display, "[ir]([ir!]*)", {}, SubTable::None},                                 // 0x1b
        Entry{"GetEvent", k_get_event, "io", {}, SubTable::None},                                        // 0x1c
        Entry{"GlobalToLocal", k_global_to_local, "o", {}, SubTable::None},                              // 0x1d
        Entry{"LocalToGlobal", k_local_to_global, "o", {}, SubTable::None},                              // 0x1e
        Entry{"MapKeyToDir", k_map_key_to_dir, "o", {}, SubTable::None},                                 // 0x1f
        Entry{"DrawMenuBar", k_draw_menu_bar, "i", {}, SubTable::None},                                  // 0x20
        Entry{"MenuSelect", k_menu_select, "o(i)", {}, SubTable::None},                                  // 0x21
        Entry{"AddMenu", k_add_menu, "rr", {}, SubTable::None},                                          // 0x22
        Entry{"DrawStatus", k_draw_status, "[r0](i)(i)", {}, SubTable::None},                            // 0x23
        Entry{"Parse", k_parse, "ro", {}, SubTable::None},                                               // 0x24
        Entry{"Said", k_said, "[r0]", {}, SubTable::None},                                               // 0x25
        Entry{"SetSynonyms", k_set_synonyms, "o", {}, SubTable::None},                                   // 0x26
        Entry{"HaveMouse", k_have_mouse, "", {}, SubTable::None},                                        // 0x27
        Entry{"SetCursor", k_set_cursor, "i(i)(i)(i)(i)", {}, SubTable::None},                           // 0x28
        Entry{"FOpen", k_file_io_open, "ri", {}, SubTable::None},                                        // 0x29
        Entry{"FPuts", k_file_io_write_string, "ir", {}, SubTable::None},                                // 0x2a
        Entry{"FGets", k_file_io_read_string, "rii", {}, SubTable::None},                                // 0x2b
        Entry{"FClose", k_file_io_close, "i", {}, SubTable::None},                                       // 0x2c
        Entry{"SaveGame", k_save_game, "[r0]i[r0](r0)", {}, SubTable::None},                             // 0x2d
        Entry{"RestoreGame", k_restore_game, "[r0]i[r0]", {}, SubTable::None},                           // 0x2e
        Entry{"RestartGame", k_restart_game, "", {}, SubTable::None},                                    // 0x2f
        Entry{"GameIsRestarting", k_game_is_restarting, "(i)", {}, SubTable::None},                      // 0x30
        Entry{"DoSound", k_do_sound, "i(.*)", {}, SubTable::DoSound},                                    // 0x31
        Entry{"NewList", k_new_list, "", {}, SubTable::None},                                            // 0x32
        Entry{"DisposeList", k_dispose_list, "l", {}, SubTable::None},                                   // 0x33
        Entry{"NewNode", k_new_node, "..", {}, SubTable::None},                                          // 0x34
        Entry{"FirstNode", k_first_node, "[l0]", {}, SubTable::None},                                    // 0x35
        Entry{"LastNode", k_last_node, "l", {}, SubTable::None},                                         // 0x36
        Entry{"EmptyList", k_empty_list, "l", {}, SubTable::None},                                       // 0x37
        Entry{"NextNode", k_next_node, "n", {}, SubTable::None},                                         // 0x38
        Entry{"PrevNode", k_prev_node, "n", {}, SubTable::None},                                         // 0x39
        Entry{"NodeValue", k_node_value, "[n0]", {}, SubTable::None},                                    // 0x3a
        Entry{"AddAfter", k_add_after, "lnn", {}, SubTable::None},                                       // 0x3b
        Entry{"AddToFront", k_add_to_front, "ln", {}, SubTable::None},                                   // 0x3c
        Entry{"AddToEnd", k_add_to_end, "ln", {}, SubTable::None},                                       // 0x3d
        Entry{"FindKey", k_find_key, "l.", {}, SubTable::None},                                          // 0x3e
        Entry{"DeleteKey", k_delete_key, "l.", {}, SubTable::None},                                      // 0x3f
        Entry{"Random", k_random, "(i)(i)", {}, SubTable::None},                                         // 0x40
        Entry{"Abs", k_abs, "i", {}, SubTable::None},                                                    // 0x41
        Entry{"Sqrt", k_sqrt, "i", {}, SubTable::None},                                                  // 0x42
        Entry{"GetAngle", k_get_angle, "iiii", {}, SubTable::None},                                      // 0x43
        Entry{"GetDistance", k_get_distance, "ii(i)(i)(i)(i)", {}, SubTable::None},                      // 0x44
        Entry{"Wait", k_wait, "i", {}, SubTable::None},                                                  // 0x45
        Entry{"GetTime", k_get_time, "(i)", {}, SubTable::None},                                         // 0x46
        Entry{"StrEnd", k_str_end, "r", {}, SubTable::None},                                             // 0x47
        Entry{"StrCat", k_str_cat, "rr", {}, SubTable::None},                                            // 0x48
        Entry{"StrCmp", k_str_cmp, "rr(i)", {}, SubTable::None},                                         // 0x49
        Entry{"StrLen", k_str_len, "[r0]", {}, SubTable::None},                                          // 0x4a
        Entry{"StrCpy", k_str_cpy, "r[r0](i)", {}, SubTable::None},                                      // 0x4b
        Entry{"Format", k_format, "r[ri](.*)", {}, SubTable::None},                                      // 0x4c
        Entry{"GetFarText", k_get_far_text, "ii[r0]", {}, SubTable::None},                               // 0x4d
        Entry{"ReadNumber", k_read_number, "r", {}, SubTable::None},                                     // 0x4e
        Entry{"BaseSetter", k_base_setter, "o", {}, SubTable::None},                                     // 0x4f
        Entry{"DirLoop", k_dir_loop, "oi", {}, SubTable::None},                                          // 0x50
        Entry{"CanBeHere", k_can_be_here, "o(l)", {}, SubTable::None},                                   // 0x51
        Entry{"OnControl", k_on_control, "ii(i)(i)(i)", {}, SubTable::None},                             // 0x52
        Entry{"InitBresen", k_init_bresen, "o(i)", {}, SubTable::None},                                  // 0x53
        Entry{"DoBresen", k_do_bresen, "o", {}, SubTable::None},                                         // 0x54
        Entry{"DoAvoider", k_do_avoider, "o(i)", {}, SubTable::None},                                    // 0x55
        Entry{"SetJump", k_set_jump, "oiii", {}, SubTable::None},                                        // 0x56
        Entry{"SetDebug", k_set_debug, "(i*)", {}, SubTable::None},                                      // 0x57
        Entry{"InspectObj", k_dummy, "(.*)", {}, SubTable::None},                                        // 0x58
        Entry{"ShowSends", k_dummy, "(.*)", {}, SubTable::None},                                         // 0x59
        Entry{"ShowObjs", k_dummy, "(.*)", {}, SubTable::None},                                          // 0x5a
        Entry{"ShowFree", k_dummy, "(.*)", {}, SubTable::None},                                          // 0x5b
        Entry{"MemoryInfo", k_memory_info, "i", {}, SubTable::None},                                     // 0x5c
        Entry{"StackUsage", k_dummy, "(.*)", {}, SubTable::None},                                        // 0x5d
        Entry{"Profiler", k_dummy, "(.*)", {}, SubTable::None},                                          // 0x5e
        Entry{"GetMenu", k_get_menu, "i.", {}, SubTable::None},                                          // 0x5f
        Entry{"SetMenu", k_set_menu, "i(.*)", {}, SubTable::None},                                       // 0x60
        Entry{"GetSaveFiles", k_get_save_files, "rrr", {}, SubTable::None},                              // 0x61
        Entry{"GetCWD", k_get_cwd, "r", {}, SubTable::None},                                             // 0x62
        Entry{"CheckFreeSpace", k_check_free_space, "r", {}, SubTable::None},                            // 0x63
        Entry{"ValidPath", k_valid_path, "r", {}, SubTable::None},                                       // 0x64
        Entry{"CoordPri", k_coord_pri, "i(i)", {}, SubTable::None},                                      // 0x65
        Entry{"StrAt", k_str_at, "ri(i)", {}, SubTable::None},                                           // 0x66
        Entry{"DeviceInfo", k_device_info, "i(r)(r)(i)", {}, SubTable::None},                            // 0x67
        Entry{"GetSaveDir", k_get_save_dir, "", {}, SubTable::None},                                     // 0x68
        Entry{"CheckSaveGame", k_check_save_game, ".*", {}, SubTable::None},                             // 0x69
        Entry{"ShakeScreen", k_shake_screen, "(i)(i)", {}, SubTable::None},                              // 0x6a
        Entry{"FlushResources", k_flush_resources, "i", {}, SubTable::None},                             // 0x6b
        Entry{"SinMult", k_times_sin, "ii", {}, SubTable::None},                                         // 0x6c
        Entry{"CosMult", k_times_cos, "ii", {}, SubTable::None},                                         // 0x6d
        Entry{"SinDiv", k_sin_div, "ii", {}, SubTable::None},                                            // 0x6e
        Entry{"CosDiv", k_cos_div, "ii", {}, SubTable::None},                                            // 0x6f
        Entry{"Graph", k_graph, nullptr, {}, SubTable::Graph},                                           // 0x70
        Entry{"Joystick", k_joystick, "i(.*)", {}, SubTable::None},                                      // 0x71
    };
    return table;
}

std::span<const SubEntry> sub_table(SubTable table) {
    static const std::array do_sound = {
        SubEntry{0, "DoSoundInit", k_do_sound_init, "o"},
        SubEntry{1, "DoSoundPlay", k_do_sound_play, "o"},
        SubEntry{2, "DoSoundRestore", k_empty, "(o)"},
        SubEntry{3, "DoSoundDispose", k_do_sound_dispose, "o"},
        SubEntry{4, "DoSoundMute", k_do_sound_mute, "(i)"},
        SubEntry{5, "DoSoundStop", k_do_sound_stop, "o"},
        SubEntry{6, "DoSoundPause", k_do_sound_pause, "i"},
        SubEntry{7, "DoSoundResumeAfterRestore", k_do_sound_resume_after_restore, ""},
        SubEntry{8, "DoSoundMasterVolume", k_do_sound_master_volume, "(i)"},
        SubEntry{9, "DoSoundUpdate", k_do_sound_update, "o"},
        SubEntry{10, "DoSoundFade", k_do_sound_fade, "[o0]"},
        SubEntry{11, "DoSoundGetPolyphony", k_do_sound_get_polyphony, ""},
        SubEntry{12, "DoSoundStopAll", k_do_sound_stop_all, ""},
    };
    static const std::array graph = {
        SubEntry{2, "GraphGetColorCount", k_graph_get_color_count, ""},
        SubEntry{4, "GraphDrawLine", k_graph_draw_line, "iiiii(i)(i)"},
        SubEntry{7, "GraphSaveBox", k_graph_save_box, "iiiii"},
        SubEntry{8, "GraphRestoreBox", k_graph_restore_box, "[r0!]"},
        SubEntry{9, "GraphFillBoxBackground", k_graph_fill_box_background, "iiii"},
        SubEntry{10, "GraphFillBoxForeground", k_graph_fill_box_foreground, "iiii"},
        SubEntry{11, "GraphFillBoxAny", k_graph_fill_box_any, "iiiiii(i)(i)"},
        SubEntry{12, "GraphUpdateBox", k_graph_update_box, "iiii(i)"},
        SubEntry{13, "GraphRedrawBox", k_graph_redraw_box, "iiii"},
        SubEntry{14, "GraphAdjustPriority", k_graph_adjust_priority, "ii"},
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
    Segment* segment = segments.segment(reg.segment);
    if (segment == nullptr) {
        return sig_type_error;
    }
    std::uint16_t result = 0;
    if (!segment->is_valid_offset(reg.offset)) {
        result |= sig_is_invalid;
    }
    switch (segment->type()) {
    case SegmentType::Script: {
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
        if (((type & ~sig_is_invalid) & current) == 0) {
            // An unknown type (a stale pointer) passes where invalid
            // pointers are allowed.
            if (!((type & ~sig_is_invalid) == sig_type_error && (current & sig_is_invalid) != 0)) {
                return false;
            }
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
