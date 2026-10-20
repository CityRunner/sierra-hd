// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/error.hpp"
#include "sci_test.hpp"

using sci::core::ErrorCode;
using sci::core::Result;

namespace {

Result<int> parse_positive(int value) {
    if (value <= 0) {
        return sci::core::make_error(ErrorCode::InvalidData, "value must be positive");
    }
    return value;
}

} // namespace

SCI_TEST(result_carries_value_or_error) {
    SCI_CHECK(parse_positive(3) == 3);
    const Result<int> failed = parse_positive(0);
    SCI_CHECK(!failed.has_value());
    SCI_CHECK(!failed.has_value() && failed.error().code == ErrorCode::InvalidData);
    SCI_CHECK(!failed.has_value() && failed.error().message == "value must be positive");
}

SCI_TEST(error_codes_have_names) {
    SCI_CHECK(sci::core::to_string(ErrorCode::InvalidData) == "invalid data");
    SCI_CHECK(sci::core::to_string(ErrorCode::Io) == "I/O error");
}
