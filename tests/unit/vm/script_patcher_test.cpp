// SPDX-License-Identifier: GPL-3.0-or-later
//
// The script patcher on synthetic scripts: the magic dword and its offset,
// signature matching with skips, values and selectors, patch commands and
// apply counts, as ScummVM's ScriptPatcher computes them.

#include "sci_test.hpp"
#include "script_patcher.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using sci::vm::ScriptPatchEntry;
using sci::vm::ScriptPatcher;
using sci::vm::ScriptPatchTable;
using namespace sci::vm::patch_code; // NOLINT(google-build-using-namespace)

namespace {

constexpr std::array<std::string_view, 2> names{"doit", "bigSelector"};

constexpr auto signature = std::to_array<std::uint16_t>({
    0x10,
    skip(+2),
    magic_dword,
    0x20,
    0x21,
    u16_low(0x2322),
    u16_high(0x2322),
    sel8(0),
    end,
});

constexpr auto patch = std::to_array<std::uint16_t>({
    skip(+3),
    original_byte(+4),
    adjust(+0x10),
    u16_low(0xBEEF),
    u16_high(0xBEEF),
    sel16(0),
    original_uint16(+5),
    adjust(-1),
    end,
});

constexpr auto entries = std::to_array<ScriptPatchEntry>({
    {
        .default_active = true,
        .script = 7,
        .description = "twice",
        .apply_count = 2,
        .signature = signature,
        .patch = patch,
    },
    {
        .default_active = false,
        .script = 7,
        .description = "inactive",
        .apply_count = 1,
        .signature = signature,
        .patch = patch,
    },
    {
        .default_active = true,
        .script = 8,
        .description = "once",
        .apply_count = 1,
        .signature = signature,
        .patch = patch,
    },
});

constexpr ScriptPatchTable table{.entries = entries, .selector_names = names};

ScriptPatcher make_patcher(std::vector<std::string>* faults = nullptr) {
    ScriptPatcher patcher(table);
    patcher.attach(
        [](std::string_view name) {
            if (name == "doit") {
                return 0x45;
            }
            return name == "bigSelector" ? 0x1234 : -1;
        },
        [faults](std::string message) {
            if (faults != nullptr) {
                faults->push_back(std::move(message));
            }
        });
    return patcher;
}

// The signature's bytes at `at`, with `fill` where it skips.
void put_match(std::vector<std::uint8_t>& data, std::size_t at, std::uint8_t fill) {
    const std::array<std::uint8_t, 8> bytes{0x10, fill, fill, 0x20, 0x21, 0x22, 0x23, 0x45};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        data[at + i] = bytes[i];
    }
}

} // namespace

SCI_TEST(magic_dword_is_the_four_bytes_after_the_marker) {
    const ScriptPatcher patcher = make_patcher();
    std::uint32_t magic = 0;
    int offset = 0;
    patcher.calculate_magic(signature, true, magic, offset);
    SCI_CHECK(magic == 0x23222120U);
    SCI_CHECK(offset == -3); // One byte and a skip of two before it.
}

SCI_TEST(signature_is_found_through_its_magic_dword) {
    const ScriptPatcher patcher = make_patcher();
    std::uint32_t magic = 0;
    int offset = 0;
    patcher.calculate_magic(signature, true, magic, offset);
    std::vector<std::uint8_t> data(32, 0);
    put_match(data, 9, 0x77);
    const auto found = patcher.find_signature(magic, offset, signature, data);
    SCI_CHECK(found.has_value() && *found == 9);
    SCI_CHECK(patcher.verify_signature(9, signature, data));
    SCI_CHECK(!patcher.verify_signature(8, signature, data));
}

SCI_TEST(selector_and_value_mismatches_fail_the_signature) {
    const ScriptPatcher patcher = make_patcher();
    std::uint32_t magic = 0;
    int offset = 0;
    patcher.calculate_magic(signature, true, magic, offset);
    std::vector<std::uint8_t> data(16, 0);
    put_match(data, 2, 0);
    data[9] = 0x46; // Not the selector.
    SCI_CHECK(!patcher.find_signature(magic, offset, signature, data).has_value());
    put_match(data, 2, 0);
    data[7] = 0x00; // Not the uint16.
    SCI_CHECK(!patcher.find_signature(magic, offset, signature, data).has_value());
    // A magic dword whose signature would start before the script.
    std::vector<std::uint8_t> early{0x20, 0x21, 0x22, 0x23, 0x45};
    SCI_CHECK(!patcher.find_signature(magic, offset, signature, early).has_value());
    // Running past the end.
    std::vector<std::uint8_t> cut{0x10, 0, 0, 0x20, 0x21, 0x22, 0x23};
    SCI_CHECK(!patcher.find_signature(magic, offset, signature, cut).has_value());
}

SCI_TEST(patch_writes_values_selectors_and_adjusted_original_bytes) {
    ScriptPatcher patcher = make_patcher();
    std::vector<std::uint8_t> data(20, 0);
    put_match(data, 4, 0x66);
    patcher.process(8, data);
    SCI_CHECK(patcher.applied().size() == 1);
    // At 4+3: original byte 4 (0x21) + 0x10, then BEEF, selector 0x0045,
    // then original uint16 at 5 (0x2322... = 0x2322 read from bytes 22 23)
    // less one.
    SCI_CHECK(data[7] == 0x31);
    SCI_CHECK(data[8] == 0xEF && data[9] == 0xBE);
    SCI_CHECK(data[10] == 0x45 && data[11] == 0x00);
    SCI_CHECK(data[12] == 0x21 && data[13] == 0x23); // 0x2322 - 1
    SCI_CHECK(data[4] == 0x10 && data[5] == 0x66);   // Untouched.
}

SCI_TEST(apply_count_limits_how_often_a_patch_applies) {
    ScriptPatcher patcher = make_patcher();
    std::vector<std::uint8_t> data(64, 0);
    put_match(data, 0, 1);
    put_match(data, 20, 2);
    put_match(data, 40, 3);
    patcher.process(7, data); // "twice" applies to the first two; "inactive" never.
    SCI_CHECK(patcher.applied().size() == 2);
    SCI_CHECK(data[3] == 0x31 && data[23] == 0x31);
    SCI_CHECK(data[43] == 0x20); // The third match stays.
    std::vector<std::uint8_t> other(64, 0);
    put_match(other, 0, 1);
    patcher.process(9, other); // No entry for script 9.
    SCI_CHECK(patcher.applied().size() == 2);
    SCI_CHECK(other[3] == 0x20);
}

SCI_TEST(an_8_bit_selector_with_a_16_bit_number_is_a_fault) {
    constexpr auto big = std::to_array<std::uint16_t>({magic_dword, 0x01, 0x02, 0x03, sel8(1), end});
    constexpr auto none = std::to_array<std::uint16_t>({end});
    std::vector<std::string> faults;
    const ScriptPatcher patcher = make_patcher(&faults);
    std::uint32_t magic = 0;
    int offset = 0;
    patcher.calculate_magic(big, true, magic, offset);
    patcher.calculate_magic(none, false, magic, offset);
    SCI_CHECK(faults.size() == 1);
}

SCI_TEST(laurabow1_table_is_consistent) {
    std::vector<std::string> faults;
    ScriptPatcher patcher(sci::vm::laurabow1_patches());
    patcher.attach([](std::string_view /*name*/) { return 0x10; },
                   [&faults](std::string message) { faults.push_back(std::move(message)); });
    std::vector<std::uint8_t> data(8, 0);
    patcher.process(99, data); // Builds every entry's magic dword.
    SCI_CHECK(faults.empty());
    SCI_CHECK(sci::vm::laurabow1_patches().entries.size() == 16);
}
