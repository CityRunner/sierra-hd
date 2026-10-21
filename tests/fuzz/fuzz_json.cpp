// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parses arbitrary text as JSON: the reader must return a value or an
// error, never fail otherwise.

#include "sci/core/json.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size); // NOLINT
    [[maybe_unused]] const auto parsed = sci::core::parse_json(text);
    return 0;
}
