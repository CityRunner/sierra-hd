// SPDX-License-Identifier: GPL-3.0-or-later
//
// The files a game opens (kFOpen and the other file kernel functions),
// supplied by the host: sci-vm itself does no I/O (spec section 4.2).

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sci::vm {

class FileStore {
public:
    FileStore() = default;
    FileStore(const FileStore&) = delete;
    FileStore& operator=(const FileStore&) = delete;
    FileStore(FileStore&&) = delete;
    FileStore& operator=(FileStore&&) = delete;
    virtual ~FileStore() = default;

    // A file the game reads, by its lower-case name: one the game wrote, or
    // else a file of the game folder (as ScummVM falls back to the game
    // files).
    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> read(std::string_view name) = 0;
    // Creates or replaces a file the game writes.
    virtual bool write(std::string_view name, std::span<const std::uint8_t> data) = 0;
};

// Files in memory: the game folder's files to read, and what the game
// writes, kept until the store goes. Used for deterministic runs.
class MemoryFileStore final : public FileStore {
public:
    explicit MemoryFileStore(std::map<std::string, std::vector<std::uint8_t>> game_files = {})
        : game_files_(std::move(game_files)) {}

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read(std::string_view name) override;
    bool write(std::string_view name, std::span<const std::uint8_t> data) override;

    [[nodiscard]] const std::map<std::string, std::vector<std::uint8_t>, std::less<>>& written() const {
        return written_;
    }

private:
    std::map<std::string, std::vector<std::uint8_t>> game_files_;
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> written_;
};

} // namespace sci::vm
