// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vector/hints.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstdint>
#include <string>

using sci::vector::HintPrimitive;
using sci::vector::LineEnd;
using sci::vector::parse_hints;

namespace {

// A valid file with one of everything; the tests below break it in turn.
const std::string valid = R"({
  "format": "sierra-hd-hints",
  "version": 1,
  "picture": 44,
  "pic_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "analyser": "test",
  "regions": [{"id": "sky", "at": [10, 5], "label": "night sky", "uncertain": true}],
  "chains": [
    {"id": "arch-left", "lines": [30, 31, 32], "primitive": "arc", "label": "arch"},
    {"id": "arch-right", "lines": [40, 41], "primitive": "spline"}
  ],
  "gaps": [{"id": "door-gap", "from": 72, "end": "end", "to": 90}],
  "symmetry": [{"kind": "mirror", "chains": ["arch-left", "arch-right"], "note": "both sides"}],
  "relations": [
    {"kind": "in-front-of", "a": "arch-left", "b": "sky"},
    {"kind": "contains", "a": "sky", "b": "arch-right", "uncertain": true, "note": "seen through"}
  ]
})";

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    SCI_CHECK(at != std::string::npos);
    if (at != std::string::npos) {
        text.replace(at, from.size(), to);
    }
    return text;
}

} // namespace

SCI_TEST(hints_read_a_complete_file) {
    const auto hints = parse_hints(valid);
    SCI_CHECK(hints.has_value());
    if (!hints) {
        return;
    }
    SCI_CHECK(hints->picture == 44);
    SCI_CHECK(hints->pic_sha256[0] == 0x01 && hints->pic_sha256[31] == 0xEF);
    SCI_CHECK(hints->regions.size() == 1 && hints->regions[0].x == 10 && hints->regions[0].uncertain);
    SCI_CHECK(hints->chains.size() == 2);
    SCI_CHECK(hints->chains[0].primitive == HintPrimitive::Arc);
    SCI_CHECK(hints->chains[0].lines[2] == 32);
    SCI_CHECK(!hints->chains[1].uncertain && hints->chains[1].label.empty());
    SCI_CHECK(hints->gaps.size() == 1 && hints->gaps[0].end == LineEnd::End);
    SCI_CHECK(hints->gaps[0].from == 72 && hints->gaps[0].to == 90);
    SCI_CHECK(hints->symmetry.size() == 1 && hints->symmetry[0].chains[1] == "arch-right");
    SCI_CHECK(hints->relations.size() == 2);
    SCI_CHECK(hints->relations[0].kind == sci::vector::RelationKind::InFrontOf && hints->relations[0].b == "sky");
    SCI_CHECK(hints->relations[1].kind == sci::vector::RelationKind::Contains && hints->relations[1].uncertain);
}

SCI_TEST(hints_arrays_are_optional) {
    const auto hints = parse_hints(R"({"format": "sierra-hd-hints", "version": 1, "picture": 0,
        "pic_sha256": "0000000000000000000000000000000000000000000000000000000000000000", "analyser": ""})");
    SCI_CHECK(hints.has_value() && hints->chains.empty() && hints->gaps.empty());
}

SCI_TEST(hints_reject_invalid_files) {
    const std::array<std::pair<const char*, const char*>, 22> breakages{
        {
            {R"("version": 1)", R"("version": 2)"},
            {R"("sierra-hd-hints")", R"("other")"},
            {R"("picture": 44)", R"("picture": 1000)"},
            {"0123456789abcdef\",", "0123456789abcdeF\","},
            {R"("analyser": "test")", R"("analyser": "t\u0001")"},
            {R"("at": [10, 5])", R"("at": [320, 5])"},
            {R"("at": [10, 5])", R"("at": [10])"},
            {R"("id": "sky")", R"("id": "Sky")"},
            {R"("id": "sky")", R"("id": "arch-left")"},
            {R"([30, 31, 32])", R"([30])"},
            {R"([30, 31, 32])", R"([30, 30])"},
            {R"([40, 41])", R"([40, 31])"},
            {R"("primitive": "arc")", R"("primitive": "circle")"},
            {R"("end": "end")", R"("end": "middle")"},
            {R"("to": 90)", R"("to": 72)"},
            {R"(["arch-left", "arch-right"])", R"(["arch-left", "nothing"])"},
            {R"("note": "both sides")", R"("notes": "both sides")"},
            {R"("uncertain": true)", R"("uncertain": 1)"},
            {R"("kind": "in-front-of")", R"("kind": "above")"},
            {R"("a": "arch-left", "b": "sky")", R"("a": "arch-left", "b": "moon")"},
            {R"("a": "arch-left", "b": "sky")", R"("a": "sky", "b": "sky")"},
            {
                R"("kind": "contains", "a": "sky", "b": "arch-right")",
                R"("kind": "in-front-of", "a": "arch-left", "b": "sky")",
            },
        },
    };
    for (const auto& [from, to] : breakages) {
        const auto hints = parse_hints(replaced(valid, from, to));
        SCI_CHECK(!hints.has_value());
    }
    SCI_CHECK(!parse_hints("[]").has_value());
    SCI_CHECK(!parse_hints("{").has_value());
}

SCI_TEST(hints_match_their_picture_data) {
    auto hints = parse_hints(valid);
    SCI_CHECK(hints.has_value());
    if (!hints) {
        return;
    }
    const std::array<std::uint8_t, 3> data{'a', 'b', 'c'};
    SCI_CHECK(!sci::vector::hints_match(*hints, data));
    // SHA-256 of "abc" (FIPS 180-2).
    const auto abc = parse_hints(replaced(valid, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                                          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    SCI_CHECK(abc.has_value() && sci::vector::hints_match(*abc, data));
    SCI_CHECK(sci::vector::hint_file_name(5) == "pic.005.json");
}
