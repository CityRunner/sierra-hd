// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/selector.h, engines/sci/engine/selector.cpp
//   (Kernel::mapSelectors), engines/sci/engine/kernel.cpp
//   (Kernel::loadSelectorNames) (SCI0 late paths)
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

#pragma once

#include "sci/vm/reg.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sci::vm {

// The selectors the interpreter itself uses, by name (-1 if the game has no
// such selector). The C++ name is the selector name, except where it is not
// a valid identifier.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): the selector list, expanded twice (an X-macro).
#define SCI_VM_SELECTORS(X)                                                                                            \
    X(info, "-info-")                                                                                                  \
    X(y, "y")                                                                                                          \
    X(x, "x")                                                                                                          \
    X(view, "view")                                                                                                    \
    X(loop, "loop")                                                                                                    \
    X(cel, "cel")                                                                                                      \
    X(under_bits, "underBits")                                                                                         \
    X(ns_top, "nsTop")                                                                                                 \
    X(ns_left, "nsLeft")                                                                                               \
    X(ns_bottom, "nsBottom")                                                                                           \
    X(ns_right, "nsRight")                                                                                             \
    X(ls_top, "lsTop")                                                                                                 \
    X(ls_left, "lsLeft")                                                                                               \
    X(ls_bottom, "lsBottom")                                                                                           \
    X(ls_right, "lsRight")                                                                                             \
    X(signal, "signal")                                                                                                \
    X(illegal_bits, "illegalBits")                                                                                     \
    X(br_top, "brTop")                                                                                                 \
    X(br_left, "brLeft")                                                                                               \
    X(br_bottom, "brBottom")                                                                                           \
    X(br_right, "brRight")                                                                                             \
    X(text, "text")                                                                                                    \
    X(elements, "elements")                                                                                            \
    X(mode, "mode")                                                                                                    \
    X(state, "state")                                                                                                  \
    X(font, "font")                                                                                                    \
    X(type, "type")                                                                                                    \
    X(cursor, "cursor")                                                                                                \
    X(max, "max")                                                                                                      \
    X(mark, "mark")                                                                                                    \
    X(sort, "sort")                                                                                                    \
    X(message, "message")                                                                                              \
    X(play, "play")                                                                                                    \
    X(restore, "restore")                                                                                              \
    X(number, "number")                                                                                                \
    X(handle, "handle")                                                                                                \
    X(node_ptr, "nodePtr")                                                                                             \
    X(client, "client")                                                                                                \
    X(dx, "dx")                                                                                                        \
    X(dy, "dy")                                                                                                        \
    X(b_mov_cnt, "b-moveCnt")                                                                                          \
    X(b_i1, "b-i1")                                                                                                    \
    X(b_i2, "b-i2")                                                                                                    \
    X(b_di, "b-di")                                                                                                    \
    X(b_x_axis, "b-xAxis")                                                                                             \
    X(b_incr, "b-incr")                                                                                                \
    X(x_step, "xStep")                                                                                                 \
    X(y_step, "yStep")                                                                                                 \
    X(x_last, "xLast")                                                                                                 \
    X(y_last, "yLast")                                                                                                 \
    X(move_speed, "moveSpeed")                                                                                         \
    X(can_be_here, "canBeHere")                                                                                        \
    X(heading, "heading")                                                                                              \
    X(mover, "mover")                                                                                                  \
    X(doit, "doit")                                                                                                    \
    X(is_blocked, "isBlocked")                                                                                         \
    X(looper, "looper")                                                                                                \
    X(priority, "priority")                                                                                            \
    X(modifiers, "modifiers")                                                                                          \
    X(replay, "replay")                                                                                                \
    X(word_fail, "wordFail")                                                                                           \
    X(syntax_fail, "syntaxFail")                                                                                       \
    X(claimed, "claimed")                                                                                              \
    X(delete_, "delete")                                                                                               \
    X(z, "z")                                                                                                          \
    X(set_pri, "setPri")                                                                                               \
    X(parse_lang, "parseLang")                                                                                         \
    X(print_lang, "printLang")                                                                                         \
    X(subtitle_lang, "subtitleLang")                                                                                   \
    X(size, "size")                                                                                                    \
    X(points, "points")                                                                                                \
    X(palette, "palette")                                                                                              \
    X(data_inc, "dataInc")                                                                                             \
    X(min, "min")                                                                                                      \
    X(sec, "sec")                                                                                                      \
    X(frame, "frame")                                                                                                  \
    X(vol, "vol")                                                                                                      \
    X(pri, "pri")                                                                                                      \
    X(move_done, "moveDone")                                                                                           \
    X(cant_be_here, "cantBeHere")                                                                                      \
    X(top_string, "topString")                                                                                         \
    X(flags, "flags")                                                                                                  \
    X(overlay, "overlay")                                                                                              \
    X(select, "select")                                                                                                \
    X(hands_off, "handsOff")                                                                                           \
    X(set_step, "setStep")                                                                                             \
    X(set_motion, "setMotion")                                                                                         \
    X(cycle_speed, "cycleSpeed")                                                                                       \
    X(owner, "owner")                                                                                                  \
    X(cur_pos, "curPos")                                                                                               \
    X(update, "update")                                                                                                \
    X(can_input, "canInput")                                                                                           \
    X(input, "input")                                                                                                  \
    X(controls, "controls")

struct SelectorCache {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): expands the selector list.
#define SCI_VM_SELECTOR_FIELD(field, name) Selector field = no_selector;
    // NOLINTNEXTLINE(readability-identifier-naming): delete_ stands for the selector "delete", a keyword.
    SCI_VM_SELECTORS(SCI_VM_SELECTOR_FIELD)
#undef SCI_VM_SELECTOR_FIELD
};

class SelectorTable {
public:
    // Reads the names from vocabulary 997.
    bool load(const std::vector<std::uint8_t>& vocab);

    [[nodiscard]] Selector find(std::string_view name) const;
    [[nodiscard]] std::string name(Selector selector) const;

    [[nodiscard]] std::size_t size() const { return names_.size(); }

    [[nodiscard]] const SelectorCache& cache() const { return cache_; }

private:
    std::vector<std::string> names_;
    SelectorCache cache_;
};

} // namespace sci::vm
