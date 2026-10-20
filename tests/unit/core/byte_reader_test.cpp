// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/byte_reader.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstdint>

using sci::core::ByteReader;
using sci::core::ErrorCode;

namespace {

constexpr std::array<std::uint8_t, 7> sample{0x01, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12};

} // namespace

SCI_TEST(reads_little_endian_values) {
    ByteReader reader(sample);
    SCI_CHECK(reader.read_u8() == 0x01);
    SCI_CHECK(reader.read_u16le() == 0x1234);
    SCI_CHECK(reader.read_u32le() == 0x12345678U);
    SCI_CHECK(reader.at_end());
}

SCI_TEST(read_past_end_is_invalid_data_and_does_not_advance) {
    ByteReader reader(sample);
    SCI_CHECK(reader.skip(5).has_value());
    const auto value = reader.read_u32le();
    SCI_CHECK(!value.has_value());
    SCI_CHECK(!value.has_value() && value.error().code == ErrorCode::InvalidData);
    SCI_CHECK(reader.position() == 5);
    SCI_CHECK(reader.read_u16le() == 0x1234);
}

SCI_TEST(seek_accepts_end_and_rejects_beyond) {
    ByteReader reader(sample);
    SCI_CHECK(reader.seek(sample.size()).has_value());
    SCI_CHECK(reader.at_end());
    SCI_CHECK(!reader.seek(sample.size() + 1).has_value());
    SCI_CHECK(reader.position() == sample.size());
}

SCI_TEST(read_bytes_returns_view_into_data) {
    ByteReader reader(sample);
    const auto bytes = reader.read_bytes(3);
    SCI_CHECK(bytes.has_value() && bytes->size() == 3 && bytes->data() == sample.data());
    SCI_CHECK(reader.remaining() == 4);
}

SCI_TEST(empty_data_reads_nothing) {
    ByteReader reader({});
    SCI_CHECK(reader.at_end());
    SCI_CHECK(!reader.read_u8().has_value());
    SCI_CHECK(reader.read_bytes(0).has_value());
}
