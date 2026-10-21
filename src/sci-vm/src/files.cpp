// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/engine/file.cpp (file_open, findFreeFileHandle,
//   getFileFromHandle, fgets_wrapper)
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

#include "files.hpp"

#include "faults.hpp"

#include <format>

namespace sci::vm {

std::optional<std::vector<std::uint8_t>> MemoryFileStore::read(std::string_view name) {
    if (const auto found = written_.find(name); found != written_.end()) {
        return found->second;
    }
    if (const auto found = game_files_.find(std::string(name)); found != game_files_.end()) {
        return found->second;
    }
    return std::nullopt;
}

bool MemoryFileStore::write(std::string_view name, std::span<const std::uint8_t> data) {
    written_[std::string(name)] = std::vector<std::uint8_t>(data.begin(), data.end());
    return true;
}

std::uint16_t Files::open(const std::string& name, int mode, std::string& error) {
    Handle opened;
    opened.name = name;
    if (mode == open_or_fail) {
        // A file the game wrote, else a game file.
        if (store_ != nullptr) {
            opened.in = store_->read(name);
        }
        if (!opened.in) {
            debug(std::format("file_open(open or fail): failed to open file '{}'", name));
        }
    } else if (mode == create || mode == open_or_create) {
        // Both start an empty file for writing, as ScummVM's save files do.
        if (store_ != nullptr) {
            opened.out = std::vector<std::uint8_t>{};
        }
    } else {
        error = std::format("file_open: unsupported mode {} (filename '{}')", mode, name);
        return 0xFFFF;
    }
    if (!opened.in && !opened.out) {
        return 0xFFFF;
    }
    opened.open = true;
    std::size_t number = 1;
    while (number < handles_.size() && handles_[number].open) {
        ++number;
    }
    if (number == handles_.size()) {
        handles_.emplace_back();
    }
    handles_[number] = std::move(opened);
    return static_cast<std::uint16_t>(number);
}

Files::Handle* Files::handle(std::uint16_t number) {
    if (number >= handles_.size() || !handles_[number].open) {
        warn(std::format("Attempt to use invalid/unused file handle {}", number));
        return nullptr;
    }
    return &handles_[number];
}

bool Files::close(std::uint16_t number) {
    Handle* file = handle(number);
    if (file == nullptr) {
        return false;
    }
    if (file->out && store_ != nullptr) {
        store_->write(file->name, *file->out);
    }
    *file = Handle{};
    return true;
}

std::optional<Files::Line> Files::read_line(std::uint16_t number, int max_size, std::string& error) {
    Handle* file = handle(number);
    if (file == nullptr) {
        return std::nullopt;
    }
    if (!file->in) {
        error = std::format("fgets_wrapper: Trying to read from file '{}' opened for writing", file->name);
        return std::nullopt;
    }
    std::string line;
    std::size_t bytes = 0;
    const std::vector<std::uint8_t>& data = *file->in;
    if (max_size > 1 && file->position < data.size()) {
        // SeekableReadStream::readLine: up to and with a line feed; a
        // carriage return, alone or before a line feed, ends the line too.
        char c = 0;
        while (static_cast<int>(line.size()) + 1 < max_size && c != '\n' && file->position < data.size()) {
            c = static_cast<char>(data[file->position++]);
            if (c == '\r') {
                if (file->position < data.size() && data[file->position] == '\n') {
                    ++file->position;
                }
                c = '\n';
            }
            line += c;
        }
        // Up to a NUL, as strnlen counts, then without the line feed.
        if (const auto nul = line.find('\0'); nul != std::string::npos) {
            line.resize(nul);
        }
        bytes = line.size();
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
    }
    return Line{.text = std::move(line), .bytes = bytes};
}

std::optional<std::size_t> Files::write(std::uint16_t number, std::string_view text) {
    Handle* file = handle(number);
    if (file == nullptr || !file->out) {
        return std::nullopt;
    }
    file->out->insert(file->out->end(), text.begin(), text.end());
    return text.size();
}

void Files::close_all() {
    for (std::size_t i = 1; i < handles_.size(); ++i) {
        if (handles_[i].open) {
            close(static_cast<std::uint16_t>(i));
        }
    }
    handles_.assign(5, Handle{});
}

} // namespace sci::vm
