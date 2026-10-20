// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-256 (FIPS 180-4), for the golden hashes of rendered frames and for
// identifying game files.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace sci::core {

using Sha256Digest = std::array<std::uint8_t, 32>;

class Sha256 {
public:
    Sha256() noexcept;

    void update(std::span<const std::uint8_t> data) noexcept;

    // The digest of everything passed to update(). The object must not be
    // used afterwards.
    [[nodiscard]] Sha256Digest finish() noexcept;

private:
    void compress(std::span<const std::uint8_t, 64> block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0; // In bytes.
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> data) noexcept;

// Lower-case hexadecimal.
[[nodiscard]] std::string to_hex(const Sha256Digest& digest);

} // namespace sci::core
