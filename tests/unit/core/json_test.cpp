// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/core/json.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string>

using sci::core::parse_json;

SCI_TEST(json_reads_objects_arrays_and_scalars) {
    const auto value = parse_json(R"( {"a": [1, -2, true, false, null, "x\"y\u0041"], "b": {}, "c": []} )");
    SCI_CHECK(value.has_value());
    if (!value) {
        return;
    }
    const auto* a = value->member("a");
    SCI_CHECK(a != nullptr && a->array() != nullptr && a->array()->size() == 6);
    if (a == nullptr || a->array() == nullptr || a->array()->size() != 6) {
        return;
    }
    const auto& items = *a->array();
    SCI_CHECK(items[0].integer() != nullptr && *items[0].integer() == 1);
    SCI_CHECK(items[1].integer() != nullptr && *items[1].integer() == -2);
    SCI_CHECK(items[2].boolean() != nullptr && *items[2].boolean());
    SCI_CHECK(items[3].boolean() != nullptr && !*items[3].boolean());
    SCI_CHECK(items[4].is_null());
    SCI_CHECK(items[5].string() != nullptr && *items[5].string() == "x\"yA");
    SCI_CHECK(value->member("b") != nullptr && value->member("b")->object() != nullptr);
    SCI_CHECK(value->member("missing") == nullptr);
    // Members keep their order.
    SCI_CHECK(value->object()->at(2).first == "c");
}

SCI_TEST(json_integers_cover_int64) {
    const auto max = parse_json("9223372036854775807");
    const auto min = parse_json("-9223372036854775808");
    SCI_CHECK(max.has_value() && *max->integer() == std::numeric_limits<std::int64_t>::max());
    SCI_CHECK(min.has_value() && *min->integer() == std::numeric_limits<std::int64_t>::min());
    SCI_CHECK(!parse_json("9223372036854775808").has_value());
    SCI_CHECK(!parse_json("-9223372036854775809").has_value());
}

SCI_TEST(json_rejects_what_the_formats_do_not_use) {
    const std::array<const char*, 15> invalid{
        "",
        "1.5",
        "1e3",
        "01",
        "-",
        "[1,]",
        R"({"a":1,})",
        R"({"a":1,"a":2})",
        R"("\u00e9")",
        "\"\xc3\xa9\"",
        "\"a\nb\"",
        "[1] x",
        "{a:1}",
        "tru",
        R"("unterminated)",
    };
    for (const char* text : invalid) {
        SCI_CHECK(!parse_json(text).has_value());
    }
    std::string deep(40, '[');
    deep += std::string(40, ']');
    SCI_CHECK(!parse_json(deep).has_value());
    std::string shallow(30, '[');
    shallow += std::string(30, ']');
    SCI_CHECK(parse_json(shallow).has_value());
}
