// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builders for synthetic SCI0 game data used by the sci-resource tests: map
// and volume entries, bit writers, and a reference SCI0 LZW encoder.

#pragma once

#include "sci/resource/resource_id.hpp"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace sci::test {

using Bytes = std::vector<std::uint8_t>;

inline void append_u16le(Bytes& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

inline void append_u32le(Bytes& bytes, std::uint32_t value) {
    append_u16le(bytes, value & 0xFFFFU);
    append_u16le(bytes, value >> 16U);
}

inline std::uint16_t id_word(resource::ResourceType type, std::uint16_t number) {
    return static_cast<std::uint16_t>((static_cast<std::uint32_t>(type) << 11U) | number);
}

inline void append_map_entry(Bytes& map, resource::ResourceType type, std::uint16_t number, std::uint32_t volume,
                             std::uint32_t offset) {
    append_u16le(map, id_word(type, number));
    append_u32le(map, (volume << 26U) | offset);
}

inline void append_map_terminator(Bytes& map) {
    map.insert(map.end(), 6, 0xFF);
}

// A volume entry: header (id, packed size + 4, unpacked size, method) and data.
inline void append_volume_entry(Bytes& volume, std::uint16_t id, std::uint16_t method, std::uint32_t unpacked_size,
                                std::span<const std::uint8_t> packed) {
    append_u16le(volume, id);
    append_u16le(volume, static_cast<std::uint32_t>(packed.size()) + 4U);
    append_u16le(volume, unpacked_size);
    append_u16le(volume, method);
    volume.insert(volume.end(), packed.begin(), packed.end());
}

inline void append_stored_entry(Bytes& volume, resource::ResourceType type, std::uint16_t number,
                                std::span<const std::uint8_t> data) {
    append_volume_entry(volume, id_word(type, number), 0, static_cast<std::uint32_t>(data.size()), data);
}

// Packs bit fields least significant bit first (SCI0 LZW).
class LsbBitWriter {
public:
    void write(std::uint32_t value, std::uint32_t bits) {
        for (std::uint32_t i = 0; i < bits; ++i) {
            if (bit_count_ % 8 == 0) {
                bytes_.push_back(0);
            }
            if (((value >> i) & 1U) != 0) {
                bytes_.back() = static_cast<std::uint8_t>(bytes_.back() | (1U << (bit_count_ % 8)));
            }
            ++bit_count_;
        }
    }

    [[nodiscard]] const Bytes& bytes() const { return bytes_; }

private:
    Bytes bytes_;
    std::uint32_t bit_count_ = 0;
};

// Packs bit fields most significant bit first (Huffman).
class MsbBitWriter {
public:
    void write(std::uint32_t value, std::uint32_t bits) {
        for (std::uint32_t i = bits; i > 0; --i) {
            if (bit_count_ % 8 == 0) {
                bytes_.push_back(0);
            }
            if (((value >> (i - 1)) & 1U) != 0) {
                bytes_.back() = static_cast<std::uint8_t>(bytes_.back() | (0x80U >> (bit_count_ % 8)));
            }
            ++bit_count_;
        }
    }

    [[nodiscard]] const Bytes& bytes() const { return bytes_; }

private:
    Bytes bytes_;
    std::uint32_t bit_count_ = 0;
};

// A straightforward SCI0 LZW encoder: 9- to 12-bit codes, least significant
// bit first, 256 = reset (unused here), 257 = end. The code width grows when
// the table reaches 512, 1024 and 2048 entries, and the table stops growing
// at 4096 entries, mirroring the decoder in ScummVM.
inline Bytes lzw_encode(std::span<const std::uint8_t> data) {
    LsbBitWriter writer;
    std::map<std::string, std::uint32_t> table;
    std::uint32_t table_size = 258;
    std::uint32_t code_bits = 9;
    std::uint32_t code_limit = 512;
    const auto code_of = [&](const std::string& string) {
        return string.size() == 1 ? static_cast<std::uint8_t>(string[0]) : table.at(string);
    };
    const auto add_entry = [&](const std::string& string) {
        if (table_size >= 4096) {
            return;
        }
        if (table_size == code_limit && code_bits < 12) {
            ++code_bits;
            code_limit = 1U << code_bits;
        }
        table.emplace(string, table_size++);
    };

    std::string current;
    for (const std::uint8_t byte : data) {
        std::string extended = current + static_cast<char>(byte);
        if (extended.size() == 1 || table.contains(extended)) {
            current = std::move(extended);
            continue;
        }
        writer.write(code_of(current), code_bits);
        add_entry(extended);
        current = std::string(1, static_cast<char>(byte));
    }
    if (!current.empty()) {
        writer.write(code_of(current), code_bits);
        add_entry(current + '\0');
    }
    writer.write(257, code_bits);
    return writer.bytes();
}

// A deterministic byte sequence with repetition, for round trips.
inline Bytes sample_data(std::size_t size, std::uint32_t seed) {
    Bytes bytes;
    bytes.reserve(size);
    std::uint32_t state = seed;
    while (bytes.size() < size) {
        state = (state * 1103515245U) + 12345U;
        const auto value = static_cast<std::uint8_t>((state >> 16U) & 0x0FU);
        const std::size_t run = ((state >> 24U) & 0x07U) + 1;
        for (std::size_t i = 0; i < run && bytes.size() < size; ++i) {
            bytes.push_back(static_cast<std::uint8_t>(value + (i % 3)));
        }
    }
    return bytes;
}

} // namespace sci::test
