// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/byte_reader.hpp"

#include <format>

namespace sci::core {

namespace {

std::unexpected<Error> out_of_bounds(std::size_t position, std::size_t count, std::size_t size) {
    return make_error(ErrorCode::InvalidData,
                      std::format("read of {} bytes at offset {} exceeds data size {}", count, position, size));
}

} // namespace

Result<std::span<const std::uint8_t>> ByteReader::read_bytes(std::size_t count) {
    if (count > remaining()) {
        return out_of_bounds(position_, count, data_.size());
    }
    const std::span<const std::uint8_t> bytes = data_.subspan(position_, count);
    position_ += count;
    return bytes;
}

Result<std::uint8_t> ByteReader::read_u8() {
    return read_bytes(1).transform([](std::span<const std::uint8_t> bytes) { return bytes[0]; });
}

Result<std::uint16_t> ByteReader::read_u16le() {
    // Composed in uint32_t: uint16_t operands would be promoted to int.
    return read_bytes(2).transform([](std::span<const std::uint8_t> bytes) {
        return static_cast<std::uint16_t>(std::uint32_t{bytes[0]} | (std::uint32_t{bytes[1]} << 8U));
    });
}

Result<std::uint32_t> ByteReader::read_u32le() {
    return read_bytes(4).transform([](std::span<const std::uint8_t> bytes) {
        return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
               (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
    });
}

Result<void> ByteReader::seek(std::size_t position) {
    if (position > data_.size()) {
        return make_error(ErrorCode::InvalidData,
                          std::format("seek to offset {} exceeds data size {}", position, data_.size()));
    }
    position_ = position;
    return {};
}

Result<void> ByteReader::skip(std::size_t count) {
    if (count > remaining()) {
        return out_of_bounds(position_, count, data_.size());
    }
    position_ += count;
    return {};
}

} // namespace sci::core
