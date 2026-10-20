// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bounds-checked reading of little-endian binary data. Game files are
// untrusted input (spec section 5.2), so every parser reads through this
// class: a read past the end is an InvalidData error, never undefined
// behaviour.

#pragma once

#include "sci/core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sci::core {

class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> data) noexcept : data_(data) {}

    [[nodiscard]] std::size_t position() const noexcept { return position_; }

    [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - position_; }

    [[nodiscard]] bool at_end() const noexcept { return position_ == data_.size(); }

    [[nodiscard]] Result<std::uint8_t> read_u8();
    [[nodiscard]] Result<std::uint16_t> read_u16le();
    [[nodiscard]] Result<std::uint32_t> read_u32le();

    // Returns a view of the next count bytes and advances past them.
    [[nodiscard]] Result<std::span<const std::uint8_t>> read_bytes(std::size_t count);

    // Moves to an absolute position; size() itself is a valid position.
    [[nodiscard]] Result<void> seek(std::size_t position);
    [[nodiscard]] Result<void> skip(std::size_t count);

private:
    std::span<const std::uint8_t> data_;
    std::size_t position_ = 0;
};

} // namespace sci::core
