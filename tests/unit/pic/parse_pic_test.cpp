// SPDX-License-Identifier: GPL-3.0-or-later

#include "pic_builder.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci_test.hpp"

#include <variant>

using sci::core::ErrorCode;
using sci::pic::Line;
using sci::pic::Pattern;
using sci::pic::Point;
using sci::test::PicBuilder;

namespace {

template <typename Operation>
const Operation* as(const sci::pic::PicCommand& command) {
    return std::get_if<Operation>(&command.operation);
}

} // namespace

SCI_TEST(terminator_only_gives_no_commands) {
    const auto commands = sci::pic::parse_pic(PicBuilder().end());
    SCI_CHECK(commands.has_value() && commands->empty());
}

SCI_TEST(state_commands_keep_their_values) {
    const auto commands = sci::pic::parse_pic(PicBuilder().color(17).priority(0x3C).control(0x25).no_visual().end());
    SCI_CHECK(commands.has_value() && commands->size() == 4);
    if (commands && commands->size() == 4) {
        SCI_CHECK(as<sci::pic::SetVisual>((*commands)[0])->index == 17);
        SCI_CHECK(as<sci::pic::SetPriority>((*commands)[1])->index == 0x0C); // Low nibble only.
        SCI_CHECK(as<sci::pic::SetControl>((*commands)[2])->value == 0x05);
        SCI_CHECK(as<sci::pic::DisableVisual>((*commands)[3]) != nullptr);
        SCI_CHECK((*commands)[1].offset == 2);
    }
}

SCI_TEST(absolute_coordinates_use_the_prefix_nibbles) {
    const auto commands = sci::pic::parse_pic(PicBuilder().line(300, 180, 5, 260).end());
    SCI_CHECK(commands.has_value() && commands->size() == 1);
    if (commands && commands->size() == 1) {
        const Line* line = as<Line>((*commands)[0]);
        SCI_CHECK(line != nullptr && line->from == Point{.x = 300, .y = 180} && line->to == Point{.x = 5, .y = 260});
    }
}

SCI_TEST(short_and_medium_lines_are_relative) {
    // Short: +3/-2 (0x3A: x +3, y -2), then -1/+1 (0x91: x -1, y +1).
    // Medium: y -5 (0x85), x -3 (0x80 | (128 - 3) = 0xFD), then y +2, x +4.
    PicBuilder pic;
    pic.bytes({0xF7}).absolute(100, 50).bytes({0x3A, 0x91});
    pic.bytes({0xF5}).absolute(10, 20).bytes({0x85, 0xFD, 0x02, 0x04});
    const auto commands = sci::pic::parse_pic(pic.end());
    SCI_CHECK(commands.has_value() && commands->size() == 4);
    if (commands && commands->size() == 4) {
        SCI_CHECK(as<Line>((*commands)[0])->to == Point{.x = 103, .y = 48});
        SCI_CHECK(as<Line>((*commands)[1])->to == Point{.x = 102, .y = 49});
        SCI_CHECK(as<Line>((*commands)[2])->to == Point{.x = 7, .y = 15});
        SCI_CHECK(as<Line>((*commands)[3])->from == Point{.x = 7, .y = 15});
        SCI_CHECK(as<Line>((*commands)[3])->to == Point{.x = 11, .y = 17});
    }
}

SCI_TEST(textured_patterns_read_a_texture_byte_per_point) {
    PicBuilder pic;
    pic.pattern_code(0x23).bytes({0xFA, 0x0A}).absolute(40, 30).bytes({0x14}).absolute(41, 31);
    const auto commands = sci::pic::parse_pic(pic.end());
    SCI_CHECK(commands.has_value() && commands->size() == 3);
    if (commands && commands->size() == 3) {
        const auto* first = as<Pattern>((*commands)[1]);
        const auto* second = as<Pattern>((*commands)[2]);
        SCI_CHECK(first != nullptr && first->code == 0x23 && first->texture == 5 &&
                  first->at == Point{.x = 40, .y = 30});
        SCI_CHECK(second != nullptr && second->texture == 10);
    }
}

SCI_TEST(extended_opcodes) {
    PicBuilder pic;
    pic.bytes({0xFE, 0x00, 41, 0x77, 0x02, 0x11}); // Palette entries 41 and 2.
    pic.bytes({0xFE, 0x01, 0x02});                 // Palette 2 ...
    for (int i = 0; i < 40; ++i) {
        pic.bytes({static_cast<std::uint8_t>(i)});
    }
    pic.bytes({0xFE, 0x03, 0x99}); // Mono1: skips one byte.
    pic.bytes({0xFE, 0x08, 42, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150, 160, 170});
    const auto commands = sci::pic::parse_pic(pic.end());
    SCI_CHECK(commands.has_value() && commands->size() == 3);
    if (commands && commands->size() == 3) {
        const auto* entries = as<sci::pic::SetPaletteEntries>((*commands)[0]);
        SCI_CHECK(entries != nullptr && entries->entries.size() == 2 && entries->entries[0].first == 41);
        const auto* palette = as<sci::pic::SetPalette>((*commands)[1]);
        SCI_CHECK(palette != nullptr && palette->number == 2 && palette->colors[39] == 39);
        const auto* table = as<sci::pic::SetPriorityTable>((*commands)[2]);
        SCI_CHECK(table != nullptr && table->bands[0] == 42 && table->bands[13] == 170);
    }
}

SCI_TEST(malformed_pictures_are_rejected) {
    const auto no_terminator = sci::pic::parse_pic(std::vector<std::uint8_t>{0xF0, 0x01});
    SCI_CHECK(!no_terminator && no_terminator.error().code == ErrorCode::InvalidData);

    const auto truncated = sci::pic::parse_pic(std::vector<std::uint8_t>{0xF6, 0x00, 0x10});
    SCI_CHECK(!truncated && truncated.error().code == ErrorCode::InvalidData);

    const auto bad_entry = sci::pic::parse_pic(PicBuilder().bytes({0xFE, 0x00, 160, 0x11}).end());
    SCI_CHECK(!bad_entry && bad_entry.error().code == ErrorCode::InvalidData);

    const auto unknown_extended = sci::pic::parse_pic(PicBuilder().bytes({0xFE, 0x09}).end());
    SCI_CHECK(!unknown_extended && unknown_extended.error().code == ErrorCode::Unsupported);

    const auto not_an_opcode = sci::pic::parse_pic(PicBuilder().bytes({0x12}).end());
    SCI_CHECK(!not_an_opcode && not_an_opcode.error().code == ErrorCode::InvalidData);

    const auto sci11 = sci::pic::parse_pic(std::vector<std::uint8_t>{0x26, 0x00, 0xFF});
    SCI_CHECK(!sci11 && sci11.error().code == ErrorCode::Unsupported);
}
