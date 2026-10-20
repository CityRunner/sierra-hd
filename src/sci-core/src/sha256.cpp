// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/sha256.hpp"

#include <algorithm>
#include <bit>
#include <string_view>

namespace sci::core {

namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::array<std::uint32_t, 8> initial_state{
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

} // namespace

Sha256::Sha256() noexcept : state_(initial_state) {
}

void Sha256::compress(std::span<const std::uint8_t, 64> block) noexcept {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t i = 0; i < 16; ++i) {
        schedule[i] = (std::uint32_t{block[4 * i]} << 24U) | (std::uint32_t{block[(4 * i) + 1]} << 16U) |
                      (std::uint32_t{block[(4 * i) + 2]} << 8U) | std::uint32_t{block[(4 * i) + 3]};
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t w15 = schedule[i - 15];
        const std::uint32_t w2 = schedule[i - 2];
        const std::uint32_t s0 = std::rotr(w15, 7) ^ std::rotr(w15, 18) ^ (w15 >> 3U);
        const std::uint32_t s1 = std::rotr(w2, 17) ^ std::rotr(w2, 19) ^ (w2 >> 10U);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }
    std::array<std::uint32_t, 8> v = state_;
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25);
        const std::uint32_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint32_t t1 = v[7] + s1 + choice + round_constants[i] + schedule[i];
        const std::uint32_t s0 = std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22);
        const std::uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const std::uint32_t t2 = s0 + majority;
        v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
    }
    for (std::size_t i = 0; i < 8; ++i) {
        state_[i] += v[i];
    }
}

void Sha256::update(std::span<const std::uint8_t> data) noexcept {
    length_ += data.size();
    while (!data.empty()) {
        const std::size_t taken = std::min(data.size(), buffer_.size() - buffered_);
        std::ranges::copy(data.first(taken), std::span(buffer_).subspan(buffered_).begin());
        buffered_ += taken;
        data = data.subspan(taken);
        if (buffered_ == buffer_.size()) {
            compress(buffer_);
            buffered_ = 0;
        }
    }
}

Sha256Digest Sha256::finish() noexcept {
    const std::uint64_t bits = length_ * 8;
    constexpr std::array<std::uint8_t, 1> marker{0x80};
    update(marker);
    constexpr std::array<std::uint8_t, 64> zeros{};
    const std::size_t padding = (buffered_ <= 56 ? 56 : 120) - buffered_;
    update(std::span(zeros).first(padding));
    std::array<std::uint8_t, 8> length{};
    for (std::size_t i = 0; i < 8; ++i) {
        length[i] = static_cast<std::uint8_t>((bits >> (56 - (8 * i))) & 0xFFU);
    }
    update(length);
    Sha256Digest digest{};
    for (std::size_t i = 0; i < 8; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            digest[(4 * i) + j] = static_cast<std::uint8_t>((state_[i] >> (24 - (8 * j))) & 0xFFU);
        }
    }
    return digest;
}

Sha256Digest sha256(std::span<const std::uint8_t> data) noexcept {
    Sha256 hasher;
    hasher.update(data);
    return hasher.finish();
}

std::string to_hex(const Sha256Digest& digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest) {
        text.push_back(digits[byte >> 4U]);
        text.push_back(digits[byte & 0x0FU]);
    }
    return text;
}

} // namespace sci::core
