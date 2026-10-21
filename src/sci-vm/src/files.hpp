// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/file.{h,cpp} (FileHandle, file_open, fgets_wrapper)
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

#include "sci/vm/files.hpp"
#include "sci/vm/reg.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sci::vm {

class Machine;

// kFOpen's modes (kFileOpenMode).
inline constexpr int open_or_create = 0;
inline constexpr int open_or_fail = 1;
inline constexpr int create = 2;

class Files {
public:
    void attach(Machine& /*machine*/) {}

    // No store: files cannot be opened, as when ScummVM finds none.
    void set_store(FileStore* store) { store_ = store; }

    // file_open: a handle from 1, or 0xFFFF (SIGNAL_REG) on failure. The
    // name is already in English and lower case.
    std::uint16_t open(const std::string& name, int mode, std::string& error);
    // Closes and writes what was written; false for an unused handle.
    bool close(std::uint16_t handle);
    // fgets_wrapper: a line of at most `max_size - 1` bytes without its
    // line feed, or nothing for an unused handle.
    std::optional<std::string> read_line(std::uint16_t handle, int max_size, std::string& error);
    // The bytes written, or nothing for a handle not open for writing.
    std::optional<std::size_t> write(std::uint16_t handle, std::string_view text);
    // RunGame closes all files.
    void close_all();

private:
    struct Handle {
        bool open = false;
        std::string name;
        std::optional<std::vector<std::uint8_t>> in;
        std::size_t position = 0;
        std::optional<std::vector<std::uint8_t>> out;
    };

    Handle* handle(std::uint16_t number);

    FileStore* store_ = nullptr;
    std::vector<Handle> handles_ = std::vector<Handle>(5); // Handle 0 is never used.
};

} // namespace sci::vm
