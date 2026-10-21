// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/script_patches.cpp (laurabow1Signatures and the
//   shared SCI0/SCI01 speed test patches it uses)
// and translated into this project's style, types and error handling. The
// signature and patch data are ScummVM's, word for word.
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

#include "script_patcher.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace sci::vm {

namespace {

using namespace patch_code; // NOLINT(google-build-using-namespace): the tables read like ScummVM's.

// The selectors the patches name (ScummVM's selectorNameTable entries), as
// indices in selector_names.
constexpr std::uint16_t sel_set_loop = 0;
constexpr std::uint16_t sel_init = 1;
constexpr std::uint16_t sel_ignore_actors = 2;
constexpr std::uint16_t sel_loop = 3;

constexpr std::array<std::string_view, 4> selector_names{"setLoop", "init", "ignoreActors", "loop"};

constexpr std::uint16_t magic = magic_dword;

// --- Speed tests (shared by SCI0 and SCI01 games) --------------------------
//
// Speed tests measured how many game cycles occur within a fixed time and
// stored the result in a global. The patch sets the global to a value that
// always passes and skips the test (also removing a 1-2 second delay at
// startup). Here the +sg of the earliest games became +ag/push.
constexpr auto sci01_speed_test_global_signature = std::to_array<std::uint16_t>({
    0xc1,
    skip(+1), // +ag machine-speed
    magic,
    0x36, // push
    0x35,
    0x01, // ldi 01
    0x1a, // eq?
    end,  // bnt [ skip initialization ]
});

constexpr auto sci01_speed_test_global_patch = std::to_array<std::uint16_t>({
    0x34,
    u16_low(0x00ff),
    u16_high(0x00ff), // ldi 00ff
    0xa1,
    original_byte(+1),
    adjust(0), // sag [ machine-speed = 255 ]
    0x18,      // not [ always skip initialization ]
    end,
});

// The speed test used a signed comparison, so it took nine minutes when
// kGetTime rolled over during it (possible after a restart). Sierra fixed it
// with an unsigned comparison; so does this patch, which otherwise would hang
// with the patch above when kGetTime is large.
constexpr auto sci0_speed_test_overflow_signature = std::to_array<std::uint16_t>({
    magic,
    0x8b,
    0x00, // lsl 00
    0x76, // push0
    0x43,
    skip(+1),
    0x00, // callk GetTime 00
    0x22, // lt?
    end,
});

constexpr auto sci0_speed_test_overflow_patch = std::to_array<std::uint16_t>({
    skip(+6),
    0x2a, // ult?
    end,
});

// --- Laura Bow 1 -----------------------------------------------------------

// A rare easter egg in room 4 (a plane) uses loop 3 of a view with three
// loops, which shows the statue; the right loop is 2. Broken only on PC.
// room4::init.
constexpr auto easter_egg_view_fix_signature = std::to_array<std::uint16_t>({
    0x78, // push1
    0x76, // push0
    magic,
    0x38,
    sel16(sel_set_loop), // pushi setLoop
    0x78,                // push1
    0x39,
    0x03, // pushi 3 (loop 3, view only has 3 loops)
    end,
});

constexpr auto easter_egg_view_fix_patch = std::to_array<std::uint16_t>({
    skip(+7),
    0x02, // (change loop to 2)
    end,
});

// Oiling the armor or opening its visor first moves Laura to the armor if
// she is not inside a rect (ego::inRect). The Atari ST and Amiga versions
// use a larger rect; the PC version gets the same one. openVisor::changeState
// (script 37), bug #7119.
constexpr auto armor_open_visor_fix_signature = std::to_array<std::uint16_t>({
    0x39,
    0x04, // pushi 04
    magic,
    0x39,
    0x6a, // pushi 6a (106d)
    0x38,
    u16_low(0x0096),
    u16_high(0x0096), // pushi 0096 (150d)
    0x39,
    0x6c, // pushi 6c (108d)
    0x38,
    u16_low(0x0098),
    u16_high(0x0098), // pushi 0098 (152d)
    end,
});

constexpr auto armor_open_visor_fix_patch = std::to_array<std::uint16_t>({
    skip(+2),
    0x39,
    0x68, // pushi 68 (104d)   (-2)
    0x38,
    u16_low(0x0094),
    u16_high(0x0094), // pushi 0094 (148d) (-2)
    0x39,
    0x6f, // pushi 6f (111d)   (+3)
    0x38,
    u16_low(0x009a),
    u16_high(0x009a), // pushi 009a (154d) (+2)
    end,
});

// The coordinate Laura is moved to, (152, 107), may be unreachable and lock
// the game in collisions; (152, 110) always is. openVisor::changeState and
// oiling::changeState (script 37), bug #7119.
constexpr auto armor_move_to_fix_signature = std::to_array<std::uint16_t>({
    magic,
    0x36, // push
    0x39,
    0x6b, // pushi 6B (107d)
    0x38,
    u16_low(0x0098),
    u16_high(0x0098), // pushi 98 (152d)
    0x7c,             // pushSelf
    0x81,
    0x00, // lag global[0]
    end,
});

constexpr auto armor_move_to_fix_patch = std::to_array<std::uint16_t>({
    skip(+1),
    0x39,
    0x6e, // pushi 6E (110d) - adjust x, so that no collision can occur anymore
    end,
});

// Leaving room 37 with 'E' after oiling the left arm (with the axe) leaves
// input disabled on PC: the local procedure that restores the room skips
// HandsOn in that one case. The tests that skip it are patched out. Third
// subroutine of script 37, called by Room37:handleEvent, bug #7154.
constexpr auto armor_oiling_arm_fix_signature = std::to_array<std::uint16_t>({
    magic,
    0x72,
    u16_low(0x18f3),
    u16_high(0x18f3), // lofsa valve [ included offset so that only PC version is patched ]
    0x4a,
    0x04, // send 04 [ valve hide: ]
    0x8b,
    0x34, // lsl 34  [ oil-target ]
    0x35,
    0x02, // ldi 02  [ left arm ]
    0x1c, // ne?
    0x30,
    u16_low(0x0014),
    u16_high(0x0014), // bnt 0014 [ skip HandsOn if oiled left arm ]
    skip(+0x10),      // [ more oil-target tests: left elbow, axe ]
    0x76,             // push0
    0x45,
    0x04,
    0x00, // callb proc0_4 [ HandsOn ]
    0x48, // ret
    end,
});

constexpr auto armor_oiling_arm_fix_patch = std::to_array<std::uint16_t>({
    skip(+5),
    0x33,
    0x16, // jmp 16 [ always call HandsOn ]
    end,
});

// Jeeves lights the chapel candles (room 58) in act 2, but Room58:init tests
// for global[155] == 11, which never holds; the test becomes > 11. Bug #10743.
constexpr auto chapel_candles_persistence_signature = std::to_array<std::uint16_t>({
    magic,
    0x89,
    0x9b, // lsg global[155] [ Jeeves' act 2 state ]
    0x35,
    0x0b, // ldi b
    0x1a, // eq?
    end,
});

constexpr auto chapel_candles_persistence_patch = std::to_array<std::uint16_t>({
    skip(+4),
    0x1e, // gt?
    end,
});

// In act 4 the DOS version does not set global[195] (who is in the room)
// for Lillian on the bed in room 44; the Amiga and Atari ST versions do.
// Room44:init, bug #10742.
constexpr auto lillian_bed_fix_signature = std::to_array<std::uint16_t>({
    magic,
    0x72,
    u16_low(0x10f8),
    u16_high(0x10f8), // lofsa suit2 [ only matches DOS version ]
    0x4a,
    0x14, // send 14
    skip(+8),
    0x89,
    0x76, // lsg global[118]
    0x35,
    0x02, // ldi 2
    0x12, // and
    0x30,
    u16_low(0x000d),
    u16_high(0x000d), // bnt d [ haven't seen Lillian in study ]
    0x35,
    0x01, // ldi 1
    end,
});

constexpr auto lillian_bed_fix_patch = std::to_array<std::uint16_t>({
    skip(+13),
    0x81,
    0x76, // lag global[118]
    0x7a, // push2
    0x12, // and
    0x31,
    0x0f, // bnt f [ haven't seen Lillian in study ]
    0x35,
    0x20, // ldi 20 [ Lillian ]
    0xa1,
    0xc3, // sag global[195] [ set Lillian as in the room ]
    end,
});

// Entering Laura's bedroom resets the cursor to the top right corner as at
// the start of the game: Room44:init lacks the startup test around its
// kSetCursor call. The test is added.
constexpr auto room44_cursor_fix_signature = std::to_array<std::uint16_t>({
    magic,      0x35,           0x00,       // ldi 00
    0xa1,       0xbe,                       // sag be [ global190 = 0 ]
    0x39,       sel8(sel_init),             // pushi init
    0x76,                                   // push0
    0x57,       0x37,           0x04,       // super Rm 04 [ super init: ]
    skip(+10),  0x43,           0x28, 0x08, // callk SetCursor 08 [ SetCursor 997 1 300 0 ]
    skip(+439), 0x89,           0xa5,       // lsg a5
    0x35,       0x00,                       // ldi 00
    0x1a,                                   // eq?
    0x30,                                   // bnt
    end,
});

constexpr auto room44_cursor_fix_patch = std::to_array<std::uint16_t>({
    0x39,
    sel8(sel_init), // pushi init
    0x76,           // push0
    0x57,
    0x37,
    0x04, // super Rm 04 [ super init: ]
    0x81,
    0xcb, // lag cb [ global203 == 0 when game is starting ]
    0x2f,
    0x0d, // bt 0d  [ skip SetCursor if game already started ]
    skip(+452),
    0x76, // push0
    0xa9,
    0xbe, // ssg be [ global190 = 0 ]
    0x81,
    0xa5, // lag a5
    0x2e, // bt
    end,
});

// Telling Lilly about Gertie in room 35 makes her walk off to the left (with
// PathOut, which does not avoid actors); if Laura is in the way the game
// blocks. Lillian ignores actors during that cutscene. goSee::changeState(1)
// in script 236.
constexpr auto tell_lilly_about_gertie_blocking_fix1_signature = std::to_array<std::uint16_t>({
    0x7a, // push2
    magic,
    0x38,
    u16_low(0x00c1),
    u16_high(0x00c1), // pushi 00C1h
    0x38,
    u16_low(0x008f),
    u16_high(0x008f), // pushi 008Fh
    0x38,
    sel16(sel_ignore_actors), // pushi ignoreActors
    0x78,                     // push1
    0x76,                     // push0
    end,
});

constexpr auto tell_lilly_about_gertie_blocking_fix1_patch = std::to_array<std::uint16_t>({
    skip(+11), // skip over until push0
    0x78,      // push1 (change push0 to push1)
    end,
});

// The second patch calls Lillian::ignoreActors(1) on goSee::changeState(9)
// in script 236.
constexpr auto tell_lilly_about_gertie_blocking_fix2_signature = std::to_array<std::uint16_t>({
    0x3c, // dup
    0x35,
    0x09, // ldi 09
    0x1a, // eq?
    0x30,
    u16_low(0x003f),
    u16_high(0x003f), // bnt [ret]
    0x39,
    skip(+1), // pushi view
    0x78,     // push1
    0x38,
    u16_low(0x0203),
    u16_high(0x0203), // pushi 203h (515d)
    0x38,
    skip(+2), // pushi posn
    0x7a,     // push2
    0x38,
    u16_low(0x00c9),
    u16_high(0x00c9), // pushi C9h (201d)
    magic,
    0x38,
    u16_low(0x0084),
    u16_high(0x0084), // pushi 84h (132d)
    0x72,
    skip(+2), // lofsa Lillian (different offsets for different platforms)
    0x4a,
    0x0e, // send 0Eh
    end,
});

constexpr auto tell_lilly_about_gertie_blocking_fix2_patch = std::to_array<std::uint16_t>({
    0x38,
    sel16(sel_ignore_actors), // pushi ignoreActors
    0x78,                     // push1
    0x76,                     // push0
    0x33,
    0x00,      // ldi 00 (waste 2 bytes)
    skip(+19), // skip over until send
    0x4a,
    0x14, // send 14h
    end,
});

// Over 20 commands lock up the game when entered while ego collides with an
// obstacle: CB1:doit stops ego's motion when ego seems to walk and be blocked,
// and it tests a stale kSignalHitObstacle before a new motion could start.
// Act:setMotion clears the flag when it sets a new motion; a motion that is
// really blocked sets it again. Act:setMotion, bug #10733.
constexpr auto obstacle_collision_lockups_fix_signature = std::to_array<std::uint16_t>({
    magic,
    0x30,
    u16_low(0x002f),
    u16_high(0x002f), // bnt 2f
    0x38,
    u16_low(0x00a3),
    u16_high(0x00a3), // pushi a3 [ startUpd ]
    0x76,             // push0
    0x54,
    0x04, // self 4
    0x7a, // push2 [ -info- ]
    0x76, // push0
    0x87,
    0x01, // lap param[1]
    0x4a,
    0x04, // send 4
    0x36, // push
    0x34,
    u16_low(0x8000),
    u16_high(0x8000), // ldi 8000
    0x12,             // and
    0x30,
    u16_low(0x000a),
    u16_high(0x000a), // bnt a
    0x39,
    0x56, // pushi 56 [ new ]
    0x76, // push0
    0x87,
    0x01, // lap param[1]
    0x4a,
    0x04, // send 4
    0x32,
    u16_low(0x0002),
    u16_high(0x0002), // jmp 2
    0x87,
    0x01, // lap param[1]
    0x65,
    0x4c, // aTop mover
    0x39,
    0x57, // pushi 57 [ init ]
    0x78, // push1
    0x7c, // pushSelf
    0x59,
    0x02, // &rest 2
    0x63,
    0x4c, // pToa mover
    0x4a,
    0x06, // send 6
    0x32,
    u16_low(0x0004),
    u16_high(0x0004), // jmp 4
    0x35,
    0x00, // ldi 0
    end,
});

constexpr auto obstacle_collision_lockups_fix_patch = std::to_array<std::uint16_t>({
    0x31,
    0x32, // bnt 32 [ save 1 byte ]
    0x63,
    0x1c, // pToa signal
    0x38,
    u16_low(0xfbff),
    u16_high(0xfbff), // pushi fbff
    0x12,             // and
    0x65,
    0x1c, // aTop signal [ clear kSignalHitObstacle (0400) ]
    0x38,
    u16_low(0x00a3),
    u16_high(0x00a3), // pushi a3 [ startUpd ]
    0x76,             // push0
    0x54,
    0x04, // self 4
    0x7a, // push2 [ -info- ]
    0x76, // push0
    0x87,
    0x01, // lap param[1]
    0x4a,
    0x04, // send 4
    0x38,
    u16_low(0x8000),
    u16_high(0x8000), // pushi 8000 [ save 1 byte ]
    0x12,             // and
    0x31,
    0x09, // bnt 9 [ save 1 byte ]
    0x39,
    0x56, // pushi 56 [ new ]
    0x76, // push0
    0x87,
    0x01, // lap param[1]
    0x4a,
    0x04, // send 4
    0x33,
    0x02, // jmp 2 [ save 1 byte ]
    0x87,
    0x01, // lap param[1]
    0x65,
    0x4c, // aTop mover
    0x39,
    0x57, // pushi 57 [ init ]
    0x78, // push1
    0x7c, // pushSelf
    0x59,
    0x02, // &rest 2
    0x63,
    0x4c, // pToa mover
    0x4a,
    0x06, // send 6
    0x48, // ret  [ save 4 bytes ]
    end,
});

// Walking up the attic stairs in room 47 diagonally puts ego on control $10
// facing left or right; Room47:doit only changes rooms facing north, and
// handleEvent disables input there, so the game locks up. Any direction but
// south now changes rooms. Room47:doit, bug #9949.
constexpr auto attic_stairs_lockup_fix_signature = std::to_array<std::uint16_t>({
    magic,
    0x39,
    sel8(sel_loop), // pushi loop
    0x76,           // push0
    0x81,
    0x00, // lag global[0]
    0x4a,
    0x04, // send 4 [ ego:loop? ]
    0x36, // push
    0x35,
    0x03, // ldi 03 [ facing north ]
    0x1a, // eq?
    end,
});

constexpr auto attic_stairs_lockup_fix_patch = std::to_array<std::uint16_t>({
    skip(+8),
    0x35,
    0x02, // ldi 02 [ facing south ]
    0x1c, // ne?
    end,
});

// Ego can collide with a notch in the left wall at the top of the left
// stairs in room 47, where Room47:handleEvent disables input. Input stays
// allowed on the top of the stairs ($0800) as on the stairs ($0200).
// Room47:handleEvent, bug #10879.
constexpr auto left_stairs_lockup_fix_signature = std::to_array<std::uint16_t>({
    magic,
    0x34,
    u16_low(0x0200),
    u16_high(0x0200), // ldi 0200 [ left stairs ]
    0x1a,             // eq? [ is ego entirely on the stairs? ]
    end,
});

constexpr auto left_stairs_lockup_fix_patch = std::to_array<std::uint16_t>({
    0x34,
    u16_low(0x0a00),
    u16_high(0x0a00), // ldi 0a00 [ left stairs | top of left stairs ]
    0x12,             // and [ is ego touching the stairs or the top? ]
    end,
});

// The fingerprint copy protection passes ranges 0-600 and 1-1000 to kRandom,
// so the maximum overflows the loop or cel of view 553; the ranges become
// 0-599 and 0-999. myCopy:init.
constexpr auto copy_protection_random_fix_signature = std::to_array<std::uint16_t>({
    0x38,
    u16_low(0x0258),
    u16_high(0x0258), // pushi 600d
    skip(+10),
    magic,
    0x78, // push1
    0x38,
    u16_low(0x03e8),
    u16_high(0x03e8), // pushi 1000d
    end,
});

constexpr auto copy_protection_random_fix_patch = std::to_array<std::uint16_t>({
    0x38,
    u16_low(0x0257),
    u16_high(0x0257), // pushi 599d
    skip(+10),
    0x76, // push0
    0x38,
    u16_low(0x03e7),
    u16_high(0x03e7), // pushi 999d
    end,
});

// Escape in the end-of-game "review your notes" dialog locks up the game;
// the dialog is repeated instead, as the other dialogs in room 786 do.
// Notes:changeState(1).
constexpr auto review_notes_dialog_signature = std::to_array<std::uint16_t>({
    0x39,
    0x0f, // pushi 0f
    skip(+0x20),
    magic,
    0x7a, // push2
    0x47,
    0xff,
    0x00,
    0x1e, // calle proc255_0 [ print dialog ]
    0x36, // push
    0x3c, // dup
    0x35,
    0x01, // ldi 01
    0x1a, // eq?
    end,
});

constexpr auto review_notes_dialog_patch = std::to_array<std::uint16_t>({
    skip(+0x27),
    0x31,
    0xd7, // bnt -29 [ repeat dialog if escape pressed ]
    0x36, // push
    0x78, // push1
    end,
});

constexpr auto entries = std::to_array<ScriptPatchEntry>({
    {
        .default_active = true,
        .script = 4,
        .description = "easter egg view fix",
        .apply_count = 1,
        .signature = easter_egg_view_fix_signature,
        .patch = easter_egg_view_fix_patch,
    },
    {
        .default_active = true,
        .script = 37,
        .description = "armor open visor fix",
        .apply_count = 1,
        .signature = armor_open_visor_fix_signature,
        .patch = armor_open_visor_fix_patch,
    },
    {
        .default_active = true,
        .script = 37,
        .description = "armor move to fix",
        .apply_count = 2,
        .signature = armor_move_to_fix_signature,
        .patch = armor_move_to_fix_patch,
    },
    {
        .default_active = true,
        .script = 37,
        .description = "allowing input, after oiling arm",
        .apply_count = 1,
        .signature = armor_oiling_arm_fix_signature,
        .patch = armor_oiling_arm_fix_patch,
    },
    {
        .default_active = true,
        .script = 44,
        .description = "lillian bed fix",
        .apply_count = 1,
        .signature = lillian_bed_fix_signature,
        .patch = lillian_bed_fix_patch,
    },
    {
        .default_active = true,
        .script = 44,
        .description = "room 44 cursor fix",
        .apply_count = 1,
        .signature = room44_cursor_fix_signature,
        .patch = room44_cursor_fix_patch,
    },
    {
        .default_active = true,
        .script = 47,
        .description = "attic stairs lockup fix",
        .apply_count = 1,
        .signature = attic_stairs_lockup_fix_signature,
        .patch = attic_stairs_lockup_fix_patch,
    },
    {
        .default_active = true,
        .script = 47,
        .description = "left stairs lockup fix",
        .apply_count = 3,
        .signature = left_stairs_lockup_fix_signature,
        .patch = left_stairs_lockup_fix_patch,
    },
    {
        .default_active = true,
        .script = 58,
        .description = "chapel candles persistence",
        .apply_count = 1,
        .signature = chapel_candles_persistence_signature,
        .patch = chapel_candles_persistence_patch,
    },
    {
        .default_active = true,
        .script = 99,
        .description = "disable speed test",
        .apply_count = 1,
        .signature = sci01_speed_test_global_signature,
        .patch = sci01_speed_test_global_patch,
    },
    {
        .default_active = true,
        .script = 99,
        .description = "fix speed test overflow",
        .apply_count = 1,
        .signature = sci0_speed_test_overflow_signature,
        .patch = sci0_speed_test_overflow_patch,
    },
    {
        .default_active = true,
        .script = 236,
        .description = "tell Lilly about Gertie blocking fix 1/2",
        .apply_count = 1,
        .signature = tell_lilly_about_gertie_blocking_fix1_signature,
        .patch = tell_lilly_about_gertie_blocking_fix1_patch,
    },
    {
        .default_active = true,
        .script = 236,
        .description = "tell Lilly about Gertie blocking fix 2/2",
        .apply_count = 1,
        .signature = tell_lilly_about_gertie_blocking_fix2_signature,
        .patch = tell_lilly_about_gertie_blocking_fix2_patch,
    },
    {
        .default_active = true,
        .script = 414,
        .description = "copy protection random fix",
        .apply_count = 1,
        .signature = copy_protection_random_fix_signature,
        .patch = copy_protection_random_fix_patch,
    },
    {
        .default_active = true,
        .script = 786,
        .description = "review notes dialog fix",
        .apply_count = 1,
        .signature = review_notes_dialog_signature,
        .patch = review_notes_dialog_patch,
    },
    {
        .default_active = true,
        .script = 998,
        .description = "obstacle collision lockups fix",
        .apply_count = 1,
        .signature = obstacle_collision_lockups_fix_signature,
        .patch = obstacle_collision_lockups_fix_patch,
    },
});

constexpr ScriptPatchTable table{.entries = entries, .selector_names = selector_names};

} // namespace

const ScriptPatchTable& laurabow1_patches() {
    return table;
}

} // namespace sci::vm
