// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/error.hpp"

namespace sci::core {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidData:
        return "invalid data";
    case ErrorCode::Unsupported:
        return "unsupported";
    case ErrorCode::NotFound:
        return "not found";
    case ErrorCode::Io:
        return "I/O error";
    }
    return "unknown error";
}

} // namespace sci::core
