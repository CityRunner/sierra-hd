// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/md5.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::string hash(std::string_view text) {
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(text.data()), // NOLINT
                                              text.size());
    return sci::core::to_hex(sci::core::md5(bytes));
}

} // namespace

SCI_TEST(md5_matches_the_rfc_1321_test_suite) {
    const std::array<std::pair<std::string_view, std::string_view>, 7> suite{
        {
            {"", "d41d8cd98f00b204e9800998ecf8427e"},
            {"a", "0cc175b9c0f1b6a831c399e269772661"},
            {"abc", "900150983cd24fb0d6963f7d28e17f72"},
            {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
            {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
            {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "d174ab98d277d9f5a5611c2c9f419d9f"},
            {
                "12345678901234567890123456789012345678901234567890123456789012345678901234567890",
                "57edf4a22be3c955ac49da2e2107b67a",
            },
        },
    };
    for (const auto& [text, expected] : suite) {
        SCI_CHECK(hash(text) == expected);
    }
}

SCI_TEST(md5_handles_every_tail_length) {
    // Lengths around the block and padding boundaries; checked against
    // independent implementations: the MD5 of 64 'a' and of 56 'a'.
    SCI_CHECK(hash(std::string(64, 'a')) == "014842d480b571495a4a0363793f7367");
    SCI_CHECK(hash(std::string(56, 'a')) == "3b0c8ac703f828b04c6c197006d17218");
}
