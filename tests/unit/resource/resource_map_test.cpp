// SPDX-License-Identifier: GPL-3.0-or-later

#include "builders.hpp"
#include "sci/resource/resource_map.hpp"
#include "sci_test.hpp"

#include <array>

using sci::core::ErrorCode;
using sci::resource::ResourceType;
using sci::test::Bytes;

namespace {

Bytes two_entry_map() {
    Bytes map;
    sci::test::append_map_entry(map, ResourceType::Script, 52, 1, 0x000100);
    sci::test::append_map_entry(map, ResourceType::View, 7, 3, 0x123456);
    sci::test::append_map_terminator(map);
    return map;
}

constexpr std::array<std::uint8_t, 2> volumes_1_and_3{1, 3};

} // namespace

SCI_TEST(map_entries_decode_type_number_volume_and_offset) {
    const auto entries = sci::resource::parse_sci0_map(two_entry_map());
    SCI_CHECK(entries.has_value() && entries->size() == 2);
    if (entries && entries->size() == 2) {
        const auto& view = (*entries)[1];
        SCI_CHECK(view.id.type == ResourceType::View && view.id.number == 7);
        SCI_CHECK(view.volume == 3 && view.offset == 0x123456);
    }
}

SCI_TEST(map_without_terminator_is_invalid) {
    Bytes map = two_entry_map();
    map.resize(map.size() - 6);
    const auto entries = sci::resource::parse_sci0_map(map);
    SCI_CHECK(!entries && entries.error().code == ErrorCode::InvalidData);
}

SCI_TEST(sci0_map_check_accepts_known_volumes) {
    SCI_CHECK(sci::resource::check_sci0_map(two_entry_map(), volumes_1_and_3).has_value());
}

SCI_TEST(sci0_map_check_rejects_missing_volume_as_sci01) {
    constexpr std::array<std::uint8_t, 1> only_volume_1{1};
    const auto checked = sci::resource::check_sci0_map(two_entry_map(), only_volume_1);
    SCI_CHECK(!checked && checked.error().code == ErrorCode::Unsupported);
}

SCI_TEST(sci0_map_check_rejects_later_formats) {
    Bytes sci1_map = two_entry_map();
    sci1_map.back() = 0x00;
    const auto sci1 = sci::resource::check_sci0_map(sci1_map, volumes_1_and_3);
    SCI_CHECK(!sci1 && sci1.error().code == ErrorCode::Unsupported);

    Bytes fm_towns_map = two_entry_map();
    fm_towns_map.push_back(0xFF);
    const auto fm_towns = sci::resource::check_sci0_map(fm_towns_map, volumes_1_and_3);
    SCI_CHECK(!fm_towns && fm_towns.error().code == ErrorCode::Unsupported);

    const Bytes tiny(6, 0xFF);
    const auto too_small = sci::resource::check_sci0_map(tiny, volumes_1_and_3);
    SCI_CHECK(!too_small && too_small.error().code == ErrorCode::InvalidData);
}
