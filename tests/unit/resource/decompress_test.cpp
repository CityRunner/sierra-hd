// SPDX-License-Identifier: GPL-3.0-or-later

#include "builders.hpp"
#include "sci/resource/decompress.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <span>

using sci::core::ErrorCode;
using sci::resource::Compression;
using sci::resource::decompress;
using sci::test::Bytes;

namespace {

std::uint32_t size_of(const Bytes& bytes) {
    return static_cast<std::uint32_t>(bytes.size());
}

Bytes lzw_codes(std::initializer_list<std::uint32_t> codes) {
    sci::test::LsbBitWriter writer;
    for (const std::uint32_t code : codes) {
        writer.write(code, 9);
    }
    return writer.bytes();
}

bool equals(const sci::core::Result<Bytes>& result, std::span<const std::uint8_t> expected) {
    return result.has_value() && std::ranges::equal(*result, expected);
}

// A Huffman stream with a two-node tree: bit 0 gives 'X', bit 1 escapes to a
// literal byte. The terminator byte is 'T'.
Bytes huffman_stream(std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> fields) {
    Bytes stream{2, 'T', 0x00, 0x10, 'X', 0x00};
    sci::test::MsbBitWriter writer;
    for (const auto& [value, bits] : fields) {
        writer.write(value, bits);
    }
    stream.insert(stream.end(), writer.bytes().begin(), writer.bytes().end());
    return stream;
}

} // namespace

SCI_TEST(stored_data_is_copied) {
    const Bytes input{1, 2, 3, 4, 5};
    SCI_CHECK(equals(decompress(Compression::None, input, 3, 3), std::span(input).first(3)));
    SCI_CHECK(!decompress(Compression::None, input, 3, 4)); // Sizes must agree.
    SCI_CHECK(!decompress(Compression::None, input, 6, 6)); // Beyond the volume.
}

SCI_TEST(lzw_literals_and_strings) {
    // 'A', 'B', then code 258 = "AB" (added after 'A'), then the terminator.
    const Bytes packed = lzw_codes({'A', 'B', 258, 257});
    const Bytes expected{'A', 'B', 'A', 'B'};
    SCI_CHECK(equals(decompress(Compression::Lzw, packed, size_of(packed), 4), expected));
}

SCI_TEST(lzw_string_that_references_itself) {
    // Code 258 is used right after it was added: "A" + "AA" (the KwKwK case).
    const Bytes packed = lzw_codes({'A', 258, 257});
    const Bytes expected{'A', 'A', 'A'};
    SCI_CHECK(equals(decompress(Compression::Lzw, packed, size_of(packed), 3), expected));
}

SCI_TEST(lzw_reset_restarts_the_table) {
    const Bytes packed = lzw_codes({'A', 'B', 256, 'C', 258, 257});
    // After the reset, 258 is the string added after 'C': "C" + next byte.
    const Bytes expected{'A', 'B', 'C', 'C', 'C'};
    SCI_CHECK(equals(decompress(Compression::Lzw, packed, size_of(packed), 5), expected));
}

SCI_TEST(lzw_round_trip_through_all_code_widths) {
    for (const std::size_t size : {std::size_t{1}, std::size_t{700}, std::size_t{20000}, std::size_t{65535}}) {
        const Bytes original = sci::test::sample_data(size, static_cast<std::uint32_t>(size));
        const Bytes packed = sci::test::lzw_encode(original);
        SCI_CHECK(equals(decompress(Compression::Lzw, packed, size_of(packed), size_of(original)), original));
    }
}

SCI_TEST(lzw_early_terminator_zero_fills) {
    const Bytes packed = lzw_codes({'A', 257});
    const Bytes expected{'A', 0, 0};
    SCI_CHECK(equals(decompress(Compression::Lzw, packed, size_of(packed), 3), expected));
}

SCI_TEST(lzw_errors) {
    // A code beyond the table with output still missing.
    const Bytes invalid = lzw_codes({'A', 300, 257});
    const auto invalid_result = decompress(Compression::Lzw, invalid, size_of(invalid), 2);
    SCI_CHECK(!invalid_result && invalid_result.error().code == ErrorCode::InvalidData);

    // More output than the unpacked size while packed data remains.
    const Bytes overrun = lzw_codes({'A', 'B', 'C', 257});
    const auto overrun_result = decompress(Compression::Lzw, overrun, 100, 2);
    SCI_CHECK(!overrun_result && overrun_result.error().code == ErrorCode::InvalidData);
}

SCI_TEST(huffman_tree_literals_and_terminator) {
    // 'X', 'X', literal 'Q', then the escaped terminator 'T'.
    const Bytes packed = huffman_stream({{0, 1}, {0, 1}, {1, 1}, {'Q', 8}, {1, 1}, {'T', 8}});
    const Bytes expected{'X', 'X', 'Q'};
    SCI_CHECK(equals(decompress(Compression::Huffman, packed, size_of(packed), 3), expected));
}

SCI_TEST(huffman_errors) {
    // Too much output for the unpacked size.
    const Bytes overrun = huffman_stream({{0, 1}, {0, 1}, {1, 1}, {'T', 8}});
    SCI_CHECK(!decompress(Compression::Huffman, overrun, size_of(overrun), 1));

    // A node link that leaves the node table.
    const Bytes bad_link{1, 'T', 0x00, 0x20, 0x00};
    SCI_CHECK(!decompress(Compression::Huffman, bad_link, size_of(bad_link), 1));

    // A link to the node itself, on which ScummVM would loop forever.
    const Bytes self_link{1, 'T', 0x00, 0x01, 0x00};
    SCI_CHECK(!decompress(Compression::Huffman, self_link, size_of(self_link), 1));

    // A node table longer than the data.
    const Bytes short_table{50, 'T', 0x00};
    SCI_CHECK(!decompress(Compression::Huffman, short_table, size_of(short_table), 1));
}
