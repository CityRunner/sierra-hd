// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/kernel.cpp (Kernel::loadSelectorNames,
//   Kernel::findSelector), engines/sci/engine/selector.cpp
//   (Kernel::mapSelectors)
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

#include "selectors.hpp"

#include <format>

namespace sci::vm {

namespace {

std::uint16_t read16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 1 >= data.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(static_cast<unsigned>(data[offset]) |
                                      (static_cast<unsigned>(data[offset + 1]) << 8U));
}

} // namespace

bool SelectorTable::load(const std::vector<std::uint8_t>& vocab) {
    names_.clear();
    // The count in the resource is one less than the number of names.
    const int count = read16(vocab, 0) + 1;
    for (int i = 0; i < count; ++i) {
        const std::size_t offset = read16(vocab, 2 + (static_cast<std::size_t>(i) * 2));
        const std::size_t length = read16(vocab, offset);
        if (offset + 2 + length > vocab.size()) {
            return false;
        }
        names_.emplace_back(vocab.begin() + static_cast<std::ptrdiff_t>(offset + 2),
                            vocab.begin() + static_cast<std::ptrdiff_t>(offset + 2 + length));
    }
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): expands the selector list.
#define SCI_VM_SELECTOR_FIND(field, name) cache_.field = find(name);
    SCI_VM_SELECTORS(SCI_VM_SELECTOR_FIND)
#undef SCI_VM_SELECTOR_FIND
    return true;
}

Selector SelectorTable::find(std::string_view name) const {
    for (std::size_t i = 0; i < names_.size(); ++i) {
        if (names_[i] == name) {
            return static_cast<Selector>(i);
        }
    }
    return no_selector;
}

std::string SelectorTable::name(Selector selector) const {
    if (selector < 0 || static_cast<std::size_t>(selector) >= names_.size()) {
        return std::format("{:x}", selector);
    }
    return names_[static_cast<std::size_t>(selector)];
}

} // namespace sci::vm
