// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/resource/resource_id.hpp"
#include "sci_test.hpp"

using sci::resource::ResourceId;
using sci::resource::ResourceType;

SCI_TEST(type_numbers_follow_the_sci0_table) {
    using sci::resource::resource_type_from_number;
    SCI_CHECK(resource_type_from_number(0x00) == ResourceType::View);
    SCI_CHECK(resource_type_from_number(0x82) == ResourceType::Script); // Bit 7 is ignored.
    SCI_CHECK(resource_type_from_number(0x14) == ResourceType::Translation);
    SCI_CHECK(resource_type_from_number(0x15) == ResourceType::Rave);
    SCI_CHECK(resource_type_from_number(0x16) == ResourceType::Invalid);
}

SCI_TEST(type_names_and_suffixes_match_scummvm) {
    using sci::resource::resource_type_name;
    using sci::resource::resource_type_suffix;
    SCI_CHECK(resource_type_name(ResourceType::View) == "view");
    SCI_CHECK(resource_type_name(ResourceType::Translation) == "xlate");
    SCI_CHECK(resource_type_name(ResourceType::Invalid) == "invalid");
    SCI_CHECK(resource_type_suffix(ResourceType::Script) == "scr");
    SCI_CHECK(resource_type_suffix(ResourceType::Memory).empty());
}

SCI_TEST(id_words_split_type_and_number) {
    const ResourceId id = sci::resource::resource_id_from_word((2U << 11U) | 52U);
    SCI_CHECK(id.type == ResourceType::Script && id.number == 52);
    const ResourceId invalid = sci::resource::resource_id_from_word(0xFFFF);
    SCI_CHECK(invalid.type == ResourceType::Invalid && invalid.number == 0x7FF);
}

SCI_TEST(ids_order_by_type_then_number) {
    const ResourceId view_9{.type = ResourceType::View, .number = 9};
    const ResourceId pic_1{.type = ResourceType::Pic, .number = 1};
    const ResourceId pic_2{.type = ResourceType::Pic, .number = 2};
    SCI_CHECK(view_9 < pic_1);
    SCI_CHECK(pic_1 < pic_2);
}
