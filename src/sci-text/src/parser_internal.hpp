// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/parser/grammar.cpp (ParseRule),
//   engines/sci/parser/vocabulary.cpp and said.cpp (node_major,
//       node_minor, node_is_terminal, node_terminal_value, scanForMajor)
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

#include "sci/text/parser.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::text {

// A GNF rule (ParseRule): tokens are non-terminal ids or the TOKEN_* forms.
struct ParseRule {
    int id = 0;
    std::uint32_t first_special = 0;
    std::uint32_t num_specials = 0;
    std::vector<std::uint32_t> data;
    friend bool operator==(const ParseRule&, const ParseRule&) = default;
};

// Read access to a tree whose children are indices. A missing child (-1),
// which ScummVM would dereference, reads as an empty leaf.
struct TreeView {
    std::span<const ParseNode> nodes;

    [[nodiscard]] const ParseNode& at(int index) const {
        static const ParseNode empty{};
        return index >= 0 && static_cast<std::size_t>(index) < nodes.size() ? nodes[static_cast<std::size_t>(index)]
                                                                            : empty;
    }

    [[nodiscard]] int left(int index) const { return at(index).left; }

    [[nodiscard]] int right(int index) const { return at(index).right; }

    [[nodiscard]] int major(int index) const { return at(left(index)).value; }

    [[nodiscard]] int minor(int index) const { return at(left(right(index))).value; }

    [[nodiscard]] bool is_terminal(int index) const {
        const int child = right(right(index));
        return child >= 0 && at(child).type != NodeType::Branch;
    }

    [[nodiscard]] int terminal_value(int index) const { return at(right(right(index))).value; }
};

// scanForMajor (vocabulary.cpp): the node of the given major below `tree`.
int scan_for_major(const TreeView& tree, int node, int major);

} // namespace sci::text
