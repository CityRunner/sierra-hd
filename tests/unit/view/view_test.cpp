// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/view/view.hpp"
#include "sci_test.hpp"

#include <cstdint>
#include <vector>

using sci::core::ErrorCode;

namespace {

void put16(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void put16_at(std::vector<std::uint8_t>& out, std::size_t offset, std::size_t value) {
    out[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    out[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
}

// Two loops sharing one 3x2 cel; loop 1 is mirrored. The cel is
// row 0: 1 1 2, row 1: 3 3 3 (runs 0x21, 0x12, 0x33), displaced (-2, 5),
// transparent colour 7.
std::vector<std::uint8_t> sample_view() {
    std::vector<std::uint8_t> view{2, 0};
    put16(view, 0x0002); // Mirror mask: loop 1.
    put16(view, 0);      // Version.
    put16(view, 0);      // Palette offset.
    put16(view, 0);      // Loop 0 offset (patched below).
    put16(view, 0);      // Loop 1 offset.
    const std::size_t loop = view.size();
    put16(view, 1); // One cel.
    put16(view, 0);
    put16(view, 0); // Cel offset (patched below).
    const std::size_t cel = view.size();
    put16(view, 3);
    put16(view, 2);
    view.insert(view.end(), {0xFE, 5, 7, 0x21, 0x12, 0x33});
    put16_at(view, 8, loop);
    put16_at(view, 10, loop);
    put16_at(view, loop + 4, cel);
    return view;
}

} // namespace

SCI_TEST(view_cels_decode_runs) {
    const auto view = sci::view::decode_view(sample_view());
    SCI_CHECK(view.has_value() && view->loops.size() == 2);
    if (view && view->loops.size() == 2) {
        const sci::view::Cel& cel = view->loops[0].cels.at(0);
        SCI_CHECK(cel.width == 3 && cel.height == 2 && cel.clear_key == 7);
        SCI_CHECK(cel.displace_x == -2 && cel.displace_y == 5);
        SCI_CHECK((cel.pixels == std::vector<std::uint8_t>{1, 1, 2, 3, 3, 3}));
    }
}

SCI_TEST(mirrored_loops_mirror_rows_and_displacement) {
    const auto view = sci::view::decode_view(sample_view());
    if (view && view->loops.size() == 2) {
        const sci::view::Cel& cel = view->loops[1].cels.at(0);
        SCI_CHECK(view->loops[1].mirrored && !view->loops[0].mirrored);
        SCI_CHECK(cel.displace_x == 2);
        SCI_CHECK((cel.pixels == std::vector<std::uint8_t>{2, 1, 1, 3, 3, 3}));
    }
}

SCI_TEST(malformed_views_are_rejected) {
    std::vector<std::uint8_t> truncated = sample_view();
    truncated.resize(truncated.size() - 2); // Runs end before the cel is full.
    const auto short_data = sci::view::decode_view(truncated);
    SCI_CHECK(!short_data && short_data.error().code == ErrorCode::InvalidData);

    std::vector<std::uint8_t> sci1 = sample_view();
    sci1[1] = 0x80;
    const auto later = sci::view::decode_view(sci1);
    SCI_CHECK(!later && later.error().code == ErrorCode::Unsupported);
}
