// SPDX-License-Identifier: GPL-3.0-or-later
//
// MD5 (RFC 1321), only to recognise game releases by the hashes in
// ScummVM's detection tables (spec section 10.2). It is not used for
// anything that needs a secure hash.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace sci::core {

using Md5Digest = std::array<std::uint8_t, 16>;

[[nodiscard]] Md5Digest md5(std::span<const std::uint8_t> data) noexcept;

[[nodiscard]] std::string to_hex(const Md5Digest& digest);

} // namespace sci::core
