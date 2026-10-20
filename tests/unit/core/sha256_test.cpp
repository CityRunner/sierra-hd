// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/executor.hpp"
#include "sci/core/sha256.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string hash_of(std::string_view text) {
    const std::span bytes(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()); // NOLINT
    return sci::core::to_hex(sci::core::sha256(bytes));
}

} // namespace

SCI_TEST(sha256_matches_the_fips_test_vectors) {
    SCI_CHECK(hash_of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    SCI_CHECK(hash_of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    SCI_CHECK(hash_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // One million 'a', fed in pieces that cross the block boundaries.
    sci::core::Sha256 hasher;
    const std::vector<std::uint8_t> piece(999, 'a');
    std::size_t left = 1'000'000;
    while (left > 0) {
        const std::size_t size = std::min(left, piece.size());
        hasher.update(std::span(piece).first(size));
        left -= size;
    }
    SCI_CHECK(sci::core::to_hex(hasher.finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

SCI_TEST(serial_executor_runs_every_task_in_order) {
    sci::core::SerialExecutor executor;
    std::vector<std::size_t> order;
    executor.run(4, [&](std::size_t i) { order.push_back(i); });
    SCI_CHECK((order == std::vector<std::size_t>{0, 1, 2, 3}));
}
