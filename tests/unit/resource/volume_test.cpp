// SPDX-License-Identifier: GPL-3.0-or-later

#include "builders.hpp"
#include "sci/resource/volume.hpp"
#include "sci_test.hpp"

#include <array>

using sci::core::ErrorCode;
using sci::resource::Compression;
using sci::resource::ResourceType;
using sci::test::Bytes;

namespace {

constexpr std::array<std::uint8_t, 3> abc{'a', 'b', 'c'};

} // namespace

SCI_TEST(volume_entry_header_fields) {
    Bytes volume(5, 0);
    sci::test::append_volume_entry(volume, sci::test::id_word(ResourceType::Pic, 12), 2, 40, abc);
    const auto header = sci::resource::read_volume_entry_header(volume, 5);
    SCI_CHECK(header.has_value());
    if (header) {
        SCI_CHECK(header->id.type == ResourceType::Pic && header->id.number == 12);
        SCI_CHECK(header->packed_size == 3 && header->unpacked_size == 40 && header->method == 2);
    }
}

SCI_TEST(volume_entry_header_rejects_bad_input) {
    const Bytes empty;
    SCI_CHECK(!sci::resource::read_volume_entry_header(empty, 0));

    Bytes truncated;
    sci::test::append_u16le(truncated, 0);
    SCI_CHECK(!sci::resource::read_volume_entry_header(truncated, 0));

    Bytes small_packed_size;
    for (const std::uint32_t field : {0U, 3U, 0U, 0U}) {
        sci::test::append_u16le(small_packed_size, field);
    }
    const auto header = sci::resource::read_volume_entry_header(small_packed_size, 0);
    SCI_CHECK(!header && header.error().code == ErrorCode::InvalidData);
}

SCI_TEST(sci0_compression_methods) {
    SCI_CHECK(sci::resource::sci0_compression(0) == Compression::None);
    SCI_CHECK(sci::resource::sci0_compression(1) == Compression::Lzw);
    SCI_CHECK(sci::resource::sci0_compression(2) == Compression::Huffman);
    const auto later = sci::resource::sci0_compression(3);
    SCI_CHECK(!later && later.error().code == ErrorCode::Unsupported);
    const auto unknown = sci::resource::sci0_compression(7);
    SCI_CHECK(!unknown && unknown.error().code == ErrorCode::InvalidData);
}

SCI_TEST(sci0_volume_check) {
    Bytes volume;
    sci::test::append_stored_entry(volume, ResourceType::Text, 1, abc);
    sci::test::append_volume_entry(volume, sci::test::id_word(ResourceType::Pic, 2), 1, 10, abc);
    SCI_CHECK(sci::resource::check_sci0_volume(volume).has_value());

    // A truncated last header ends the check without error, as in ScummVM.
    Bytes truncated = volume;
    truncated.push_back(0);
    SCI_CHECK(sci::resource::check_sci0_volume(truncated).has_value());

    // Stored data must be exactly the unpacked size.
    Bytes inconsistent;
    sci::test::append_volume_entry(inconsistent, 0, 0, 5, abc);
    const auto checked = sci::resource::check_sci0_volume(inconsistent);
    SCI_CHECK(!checked && checked.error().code == ErrorCode::Unsupported);
}
