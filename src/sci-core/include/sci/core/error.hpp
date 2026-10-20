// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project-wide error type (docs/ARCHITECTURE.md, "Error handling").
// Every operation that can fail on its input returns Result<T>; there are no
// exceptions anywhere in the project. Broken invariants are not errors: they
// stop the program through SCI_ASSERT (sci/core/assert.hpp).

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace sci::core {

// What kind of failure an Error reports. Callers branch on the code; the
// message is for logs and for the user.
enum class ErrorCode : std::int8_t {
    InvalidData, // The input violates its format: truncated, inconsistent, out of range.
    Unsupported, // Valid input or environment that this program does not handle.
    NotFound,    // A requested resource, file or entry does not exist.
    Io,          // The operating system reported a failure (app layer only).
};

struct Error {
    ErrorCode code;
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> make_error(ErrorCode code, std::string message) {
    return std::unexpected<Error>(Error{.code = code, .message = std::move(message)});
}

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

} // namespace sci::core
