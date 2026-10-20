// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   engines/sci/resource/decompressor.cpp (Decompressor, DecompressorHuffman,
//       DecompressorLZW::unpackLZW)
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

#include "sci/resource/decompress.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace sci::resource {

namespace {

using core::ErrorCode;
using core::make_error;
using core::Result;

// Reads bytes and bit fields as ScummVM's Decompressor does: whole bytes are
// fetched into a 32-bit buffer until it holds more than 24 bits, bytes past
// the end of the input read as zero, and every fetched byte is counted.
class BitReader {
public:
    explicit BitReader(std::span<const std::uint8_t> input) noexcept : input_(input) {}

    // A byte read straight from the stream, outside the bit buffer and not
    // counted (ScummVM's _src->readByte()).
    std::uint8_t read_raw_byte() noexcept {
        const std::uint8_t value = position_ < input_.size() ? input_[position_] : std::uint8_t{0};
        ++position_;
        return value;
    }

    // Bytes read straight from the stream (ScummVM's _src->read()); nullopt if
    // the input ends first, where ScummVM would leave memory uninitialised.
    std::optional<std::span<const std::uint8_t>> read_raw_bytes(std::size_t count) noexcept {
        if (position_ > input_.size() || count > input_.size() - position_) {
            return std::nullopt;
        }
        const std::span<const std::uint8_t> bytes = input_.subspan(position_, count);
        position_ += count;
        return bytes;
    }

    std::uint32_t get_bits_lsb(std::uint32_t count) noexcept {
        if (bit_count_ < count) {
            while (bit_count_ <= 24) {
                bits_ |= std::uint32_t{read_raw_byte()} << bit_count_;
                bit_count_ += 8;
                ++bytes_fetched_;
            }
        }
        const std::uint32_t value = bits_ & ~(0xFFFFFFFFU << count);
        bits_ >>= count;
        bit_count_ -= count;
        return value;
    }

    std::uint32_t get_bits_msb(std::uint32_t count) noexcept {
        if (bit_count_ < count) {
            while (bit_count_ <= 24) {
                bits_ |= std::uint32_t{read_raw_byte()} << (24 - bit_count_);
                bit_count_ += 8;
                ++bytes_fetched_;
            }
        }
        const std::uint32_t value = bits_ >> (32 - count);
        bits_ <<= count;
        bit_count_ -= count;
        return value;
    }

    [[nodiscard]] std::uint32_t bytes_fetched() const noexcept { return bytes_fetched_; }

private:
    std::span<const std::uint8_t> input_;
    std::size_t position_ = 0;
    std::uint32_t bits_ = 0;
    std::uint32_t bit_count_ = 0;
    std::uint32_t bytes_fetched_ = 0;
};

// The output buffer with ScummVM's end condition (Decompressor::isFinished).
// ScummVM writes past the buffer when the data asks for more bytes than the
// unpacked size; here that write is refused and reported instead.
class Output {
public:
    Output(std::uint32_t packed_size, std::uint32_t unpacked_size)
        : packed_size_(packed_size), unpacked_size_(unpacked_size) {
        bytes_.reserve(unpacked_size);
    }

    [[nodiscard]] bool put(std::uint8_t value) {
        if (bytes_.size() >= unpacked_size_) {
            return false;
        }
        bytes_.push_back(value);
        return true;
    }

    [[nodiscard]] bool finished(const BitReader& reader) const noexcept {
        return bytes_.size() == unpacked_size_ && reader.bytes_fetched() >= packed_size_;
    }

    [[nodiscard]] bool complete() const noexcept { return bytes_.size() == unpacked_size_; }

    [[nodiscard]] std::size_t written() const noexcept { return bytes_.size(); }

    [[nodiscard]] std::uint8_t at(std::size_t index) const noexcept { return bytes_[index]; }

    [[nodiscard]] std::vector<std::uint8_t> take() noexcept { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t packed_size_;
    std::uint32_t unpacked_size_;
};

std::unexpected<core::Error> overrun(std::string_view method) {
    return make_error(ErrorCode::InvalidData,
                      std::format("{} data produces more bytes than the unpacked size", method));
}

std::unexpected<core::Error> size_mismatch(std::string_view method, std::size_t written, std::uint32_t expected) {
    return make_error(ErrorCode::InvalidData,
                      std::format("{} data decodes to {} bytes, expected {}", method, written, expected));
}

// Decompressor::unpack: the packed bytes are the data. ScummVM copies
// packed_size bytes into an unpacked_size buffer, so other sizes would
// overflow it or leave it partly uninitialised; they are rejected here.
Result<std::vector<std::uint8_t>> unpack_stored(std::span<const std::uint8_t> input, std::uint32_t packed_size,
                                                std::uint32_t unpacked_size) {
    if (packed_size != unpacked_size) {
        return make_error(ErrorCode::InvalidData, std::format("stored resource has packed size {} but unpacked size {}",
                                                              packed_size, unpacked_size));
    }
    if (packed_size > input.size()) {
        return make_error(ErrorCode::InvalidData,
                          std::format("stored resource of {} bytes exceeds the {} bytes left in the volume",
                                      packed_size, input.size()));
    }
    const std::span<const std::uint8_t> data = input.first(packed_size);
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

// DecompressorHuffman::getc2: walks the node table from the root. A node
// whose second byte is zero is a leaf holding a byte value; otherwise its
// nibbles give the distance to the next node for bit 0 (high nibble) and bit
// 1 (low nibble), and a low nibble of zero escapes to a literal byte, which
// is returned with bit 8 set. Returns nullopt where ScummVM would read
// outside the node table.
std::optional<std::uint16_t> next_huffman_symbol(std::span<const std::uint8_t> nodes, BitReader& reader) {
    std::size_t node = 0;
    while (true) {
        if (node + 1 >= nodes.size()) {
            return std::nullopt;
        }
        const std::uint8_t links = nodes[node + 1];
        if (links == 0) {
            return nodes[node];
        }
        std::uint32_t next = 0;
        if (reader.get_bits_msb(1) != 0) {
            next = links & 0x0FU;
            if (next == 0) {
                return static_cast<std::uint16_t>(reader.get_bits_msb(8) | 0x100U);
            }
        } else {
            next = static_cast<std::uint32_t>(links >> 4U);
        }
        node += std::size_t{next} * 2;
    }
}

// DecompressorHuffman::unpack. The header (node count, terminator, node
// table) is read straight from the stream and is not counted as packed data
// fetched, exactly as in ScummVM.
Result<std::vector<std::uint8_t>> unpack_huffman(std::span<const std::uint8_t> input, std::uint32_t packed_size,
                                                 std::uint32_t unpacked_size) {
    BitReader reader(input);
    const std::size_t node_count = reader.read_raw_byte();
    const auto terminator = static_cast<std::uint16_t>(reader.read_raw_byte() | 0x100U);
    const auto nodes = reader.read_raw_bytes(node_count * 2);
    if (!nodes) {
        return make_error(ErrorCode::InvalidData, "Huffman node table exceeds the volume");
    }

    Output output(packed_size, unpacked_size);
    while (true) {
        const std::optional<std::uint16_t> symbol = next_huffman_symbol(*nodes, reader);
        if (!symbol) {
            return make_error(ErrorCode::InvalidData, "Huffman code points outside the node table");
        }
        if (*symbol == terminator || output.finished(reader)) {
            break;
        }
        // Literal escapes carry bit 8; ScummVM's putByte keeps the low byte.
        if (!output.put(static_cast<std::uint8_t>(*symbol & 0xFFU))) {
            return overrun("Huffman");
        }
    }

    if (!output.complete()) {
        return size_mismatch("Huffman", output.written(), unpacked_size);
    }
    return output.take();
}

// The string table of the SCI0 LZW decoder: codes 0-255 are literals, 256
// resets the table, 257 terminates, and codes from 258 name earlier output
// as (offset, length). Offsets and lengths are 16-bit, as in ScummVM.
class LzwTable {
public:
    static constexpr std::uint32_t reset_code = 256;
    static constexpr std::uint32_t terminator_code = 257;
    static constexpr std::uint32_t first_string_code = 258;
    static constexpr std::uint32_t max_size = 4096;

    void reset() noexcept {
        code_bits_ = 9;
        size_ = first_string_code;
        code_limit_ = 512;
    }

    [[nodiscard]] std::uint32_t code_bits() const noexcept { return code_bits_; }

    [[nodiscard]] std::uint32_t size() const noexcept { return size_; }

    [[nodiscard]] std::uint16_t offset(std::uint32_t code) const noexcept { return offsets_[code]; }

    [[nodiscard]] std::uint16_t length(std::uint32_t code) const noexcept { return lengths_[code]; }

    // Adds the string that started at `start` and runs one byte past the
    // output written so far, widening the codes first once the table reaches
    // the current code limit. A full table stays as it is.
    void append(std::size_t start, std::size_t written) noexcept {
        if (size_ >= max_size) {
            return;
        }
        if (size_ == code_limit_ && code_bits_ < 12) {
            ++code_bits_;
            code_limit_ = 1U << code_bits_;
        }
        offsets_[size_] = static_cast<std::uint16_t>(start);
        lengths_[size_] = static_cast<std::uint16_t>(written - start + 1);
        ++size_;
    }

private:
    std::array<std::uint16_t, max_size> offsets_{};
    std::array<std::uint16_t, max_size> lengths_{};
    std::uint32_t code_bits_ = 9;
    std::uint32_t size_ = first_string_code;
    std::uint32_t code_limit_ = 512;
};

enum class LzwStep : std::int8_t { Continue, Terminated, InvalidCode, Overrun };

// One code of DecompressorLZW::unpackLZW (SCI0 variant, kCompLZW).
LzwStep decode_lzw_code(LzwTable& table, BitReader& reader, Output& output) {
    const std::uint32_t code = reader.get_bits_lsb(table.code_bits());
    if (code >= table.size()) {
        return LzwStep::InvalidCode;
    }
    if (code == LzwTable::terminator_code) {
        return LzwStep::Terminated;
    }
    if (code == LzwTable::reset_code) {
        table.reset();
        return LzwStep::Continue;
    }

    const std::size_t start = output.written();
    if (code <= 0xFFU) {
        if (!output.put(static_cast<std::uint8_t>(code))) {
            return LzwStep::Overrun;
        }
    } else {
        // The string may end at the byte being written; the bytes are copied
        // one at a time, so that byte exists by the time it is read.
        const std::size_t offset = table.offset(code);
        for (std::size_t i = 0; i < table.length(code) && !output.finished(reader); ++i) {
            if (!output.put(output.at(offset + i))) {
                return LzwStep::Overrun;
            }
        }
    }
    table.append(start, output.written());
    return LzwStep::Continue;
}

Result<std::vector<std::uint8_t>> unpack_lzw(std::span<const std::uint8_t> input, std::uint32_t packed_size,
                                             std::uint32_t unpacked_size) {
    BitReader reader(input);
    Output output(packed_size, unpacked_size);
    // The table is 16 KiB; it lives on the heap to keep the stack small.
    const auto table = std::make_unique<LzwTable>();

    bool terminated = false;
    while (!output.finished(reader)) {
        const LzwStep step = decode_lzw_code(*table, reader, output);
        if (step == LzwStep::Overrun) {
            return overrun("LZW");
        }
        if (step == LzwStep::Terminated) {
            terminated = true;
            break;
        }
        // ScummVM stops at an invalid code with a warning; the result then
        // depends on the size check below, as there.
        if (step == LzwStep::InvalidCode) {
            break;
        }
    }

    // Reaching the terminator before the unpacked size counts as success in
    // ScummVM; the rest of its buffer is then uninitialised. The buffer is
    // zero-filled here instead, which only matters for data that the
    // reference cannot decode reproducibly either.
    if (!terminated && !output.complete()) {
        return size_mismatch("LZW", output.written(), unpacked_size);
    }
    std::vector<std::uint8_t> bytes = output.take();
    bytes.resize(unpacked_size, 0);
    return bytes;
}

} // namespace

Result<std::vector<std::uint8_t>> decompress(Compression method, std::span<const std::uint8_t> input,
                                             std::uint32_t packed_size, std::uint32_t unpacked_size) {
    switch (method) {
    case Compression::None:
        return unpack_stored(input, packed_size, unpacked_size);
    case Compression::Lzw:
        return unpack_lzw(input, packed_size, unpacked_size);
    case Compression::Huffman:
        return unpack_huffman(input, packed_size, unpacked_size);
    }
    return make_error(ErrorCode::Unsupported, "unknown compression method");
}

} // namespace sci::resource
