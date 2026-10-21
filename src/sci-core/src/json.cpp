// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/json.hpp"

#include <format>
#include <limits>

namespace sci::core {

const JsonValue* JsonValue::member(std::string_view name) const {
    const JsonObject* members = object();
    if (members == nullptr) {
        return nullptr;
    }
    for (const auto& [key, value] : *members) {
        if (key == name) {
            return &value;
        }
    }
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Result<JsonValue> document() {
        auto value = parse_value(0);
        if (!value) {
            return value;
        }
        skip_space();
        if (position_ != text_.size()) {
            return fail("text after the value");
        }
        return value;
    }

private:
    [[nodiscard]] std::unexpected<Error> fail(std::string_view what) const {
        return make_error(ErrorCode::InvalidData, std::format("JSON: {} at byte {}", what, position_));
    }

    void skip_space() {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t' ||
                                            text_[position_] == '\n' || text_[position_] == '\r')) {
            ++position_;
        }
    }

    bool take(char c) {
        if (position_ < text_.size() && text_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }

    bool take_word(std::string_view word) {
        if (text_.substr(position_, word.size()) == word) {
            position_ += word.size();
            return true;
        }
        return false;
    }

    // Recursion follows the nesting of the text, which is limited to
    // json_max_depth levels.
    Result<JsonValue> parse_value(std::size_t depth) { // NOLINT(misc-no-recursion)
        if (depth > json_max_depth) {
            return fail("too deeply nested");
        }
        skip_space();
        if (position_ >= text_.size()) {
            return fail("value expected");
        }
        const char c = text_[position_];
        if (c == '{') {
            return parse_object(depth);
        }
        if (c == '[') {
            return parse_array(depth);
        }
        if (c == '"') {
            auto text = parse_string();
            if (!text) {
                return std::unexpected(text.error());
            }
            return JsonValue(std::move(*text));
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_integer();
        }
        if (take_word("true")) {
            return JsonValue(true);
        }
        if (take_word("false")) {
            return JsonValue(false);
        }
        if (take_word("null")) {
            return JsonValue(nullptr);
        }
        return fail("unexpected character");
    }

    Result<JsonValue> parse_object(std::size_t depth) { // NOLINT(misc-no-recursion)
        ++position_;                                    // {
        JsonObject members;
        skip_space();
        if (take('}')) {
            return JsonValue(std::move(members));
        }
        while (true) {
            skip_space();
            if (position_ >= text_.size() || text_[position_] != '"') {
                return fail("member name expected");
            }
            auto name = parse_string();
            if (!name) {
                return std::unexpected(name.error());
            }
            for (const auto& member : members) {
                if (member.first == *name) {
                    return fail(std::format("duplicate member \"{}\"", *name));
                }
            }
            skip_space();
            if (!take(':')) {
                return fail("':' expected");
            }
            auto value = parse_value(depth + 1);
            if (!value) {
                return value;
            }
            members.emplace_back(std::move(*name), std::move(*value));
            skip_space();
            if (take('}')) {
                return JsonValue(std::move(members));
            }
            if (!take(',')) {
                return fail("',' or '}' expected");
            }
        }
    }

    Result<JsonValue> parse_array(std::size_t depth) { // NOLINT(misc-no-recursion)
        ++position_;                                   // [
        JsonArray items;
        skip_space();
        if (take(']')) {
            return JsonValue(std::move(items));
        }
        while (true) {
            auto value = parse_value(depth + 1);
            if (!value) {
                return value;
            }
            items.push_back(std::move(*value));
            skip_space();
            if (take(']')) {
                return JsonValue(std::move(items));
            }
            if (!take(',')) {
                return fail("',' or ']' expected");
            }
        }
    }

    Result<std::string> parse_string() {
        ++position_; // "
        std::string out;
        while (position_ < text_.size()) {
            const auto c = static_cast<unsigned char>(text_[position_++]);
            if (c == '"') {
                return out;
            }
            if (c < 0x20 || c >= 0x80) {
                return fail("control or non-ASCII character in a string");
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (auto escaped = parse_escape(); escaped) {
                out.push_back(*escaped);
            } else {
                return std::unexpected(escaped.error());
            }
        }
        return fail("unterminated string");
    }

    Result<char> parse_escape() {
        if (position_ >= text_.size()) {
            return fail("unterminated escape");
        }
        switch (text_[position_++]) {
        case '"':
            return '"';
        case '\\':
            return '\\';
        case '/':
            return '/';
        case 'b':
            return '\b';
        case 'f':
            return '\f';
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case 'u':
            return parse_unicode_escape();
        default:
            return fail("unknown escape");
        }
    }

    Result<char> parse_unicode_escape() {
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            if (position_ >= text_.size()) {
                return fail("unterminated \\u escape");
            }
            const char h = text_[position_++];
            unsigned digit = 0;
            if (h >= '0' && h <= '9') {
                digit = static_cast<unsigned>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
                digit = static_cast<unsigned>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
                digit = static_cast<unsigned>(h - 'A' + 10);
            } else {
                return fail("bad \\u escape");
            }
            code = (code << 4U) | digit;
        }
        if (code >= 0x80) {
            return fail("non-ASCII \\u escape");
        }
        return static_cast<char>(code);
    }

    Result<JsonValue> parse_integer() {
        const bool negative = take('-');
        if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') {
            return fail("digit expected");
        }
        if (text_[position_] == '0' && position_ + 1 < text_.size() && text_[position_ + 1] >= '0' &&
            text_[position_ + 1] <= '9') {
            return fail("leading zero");
        }
        // Accumulated negatively, so that the minimum fits.
        std::int64_t value = 0;
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            const std::int64_t digit = text_[position_++] - '0';
            if (value < (std::numeric_limits<std::int64_t>::min() + digit) / 10) {
                return fail("integer out of range");
            }
            value = (value * 10) - digit;
        }
        if (position_ < text_.size() &&
            (text_[position_] == '.' || text_[position_] == 'e' || text_[position_] == 'E')) {
            return fail("only integers are allowed");
        }
        if (!negative) {
            if (value == std::numeric_limits<std::int64_t>::min()) {
                return fail("integer out of range");
            }
            value = -value;
        }
        return JsonValue(value);
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

} // namespace

Result<JsonValue> parse_json(std::string_view text) {
    if (text.size() > json_max_size) {
        return make_error(ErrorCode::InvalidData, "JSON: text too large");
    }
    return Parser(text).document();
}

} // namespace sci::core
