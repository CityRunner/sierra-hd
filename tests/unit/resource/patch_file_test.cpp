// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/resource/patch_file.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using sci::resource::ResourceType;

SCI_TEST(patch_candidates_in_scummvm_order) {
    const std::vector<std::string> names{
        "52.scr",   "TEXT.052", "RESOURCE.001", "SCRIPT.052", "views.v56",
        "pic.0012", "README",   "view.12",      "SCIV.EXE",   "100.p56x",
    };
    const auto candidates = sci::resource::find_patch_candidates(names);
    SCI_CHECK(candidates.size() == 4);
    if (candidates.size() == 4) {
        // view: "views.v56" matches "*.v56"; ScummVM checks the character
        // after the separator ('.'), not the separator, and reads number 0.
        SCI_CHECK(names[candidates[0].file] == "views.v56");
        SCI_CHECK(candidates[0].id.type == ResourceType::View && candidates[0].id.number == 0);
        // script: the SCI0 name first, then the SCI1 name, which wins.
        SCI_CHECK(names[candidates[1].file] == "SCRIPT.052" && candidates[1].id.number == 52);
        SCI_CHECK(names[candidates[2].file] == "52.scr" && candidates[2].id.number == 52);
        SCI_CHECK(names[candidates[3].file] == "TEXT.052" && candidates[3].id.type == ResourceType::Text);
    }
}

SCI_TEST(patch_data_offset_skips_the_header) {
    using sci::resource::patch_data_offset;
    constexpr std::array<std::uint8_t, 4> script{0x82, 0x00, 0xAA, 0xBB};
    SCI_CHECK(patch_data_offset(script, ResourceType::Script) == 2);

    constexpr std::array<std::uint8_t, 6> extra_header{0x82, 0x02, 0x00, 0x00, 0xAA, 0xBB};
    SCI_CHECK(patch_data_offset(extra_header, ResourceType::Script) == 4);

    SCI_CHECK(!patch_data_offset(script, ResourceType::Text));                       // Other type.
    SCI_CHECK(!patch_data_offset(std::span(script).first(2), ResourceType::Script)); // Too small.

    constexpr std::array<std::uint8_t, 4> header_too_long{0x82, 0x02, 0xAA, 0xBB};
    SCI_CHECK(!patch_data_offset(header_too_long, ResourceType::Script));
}
