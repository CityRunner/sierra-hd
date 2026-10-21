// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small, strict JSON reader for the project's own data files (hint
// files, spec section 6.7). Files are untrusted input: the reader checks
// everything and never fails other than by returning an error. Numbers must
// be integers in the range of int64_t; fractions and exponents are
// rejected, as the formats need none and integers keep results exact.
// Object members keep their file order; duplicate names are an error.
#pragma once

#include "sci/core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sci::core {

class JsonValue;

using JsonArray = std::vector<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

class JsonValue {
public:
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, std::string, JsonArray, JsonObject>;

    JsonValue() = default;

    explicit JsonValue(Storage value) : value_(std::move(value)) {}

    [[nodiscard]] bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }

    [[nodiscard]] const bool* boolean() const { return std::get_if<bool>(&value_); }

    [[nodiscard]] const std::int64_t* integer() const { return std::get_if<std::int64_t>(&value_); }

    [[nodiscard]] const std::string* string() const { return std::get_if<std::string>(&value_); }

    [[nodiscard]] const JsonArray* array() const { return std::get_if<JsonArray>(&value_); }

    [[nodiscard]] const JsonObject* object() const { return std::get_if<JsonObject>(&value_); }

    // The member of an object with this name, or null when there is none
    // or this is not an object.
    [[nodiscard]] const JsonValue* member(std::string_view name) const;

private:
    Storage value_ = nullptr;
};

inline constexpr std::size_t json_max_depth = 32;
inline constexpr std::size_t json_max_size = std::size_t{1} << 20U; // Bytes.

// Parses a complete JSON text (UTF-8; \u escapes outside the ASCII range
// are rejected, as the formats are ASCII).
[[nodiscard]] Result<JsonValue> parse_json(std::string_view text);

} // namespace sci::core
