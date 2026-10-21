// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vector/hints.hpp"

#include "sci/core/json.hpp"
#include "sci/vector/scene.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <optional>
#include <utility>

namespace sci::vector {

namespace {

using core::JsonArray;
using core::JsonValue;

// Limits that keep a damaged or hostile file small to process.
constexpr std::size_t max_items = 4096;      // Per array of the file.
constexpr std::size_t max_chain_lines = 512; // Lines per chain.
constexpr std::size_t max_label = 200;       // Characters per label or note.
constexpr std::size_t max_id = 32;
constexpr std::int64_t max_command = 1'000'000;
constexpr std::int64_t max_picture = 999;

constexpr std::array primitive_names{
    std::pair{std::string_view("line"), HintPrimitive::Line},
    std::pair{std::string_view("arc"), HintPrimitive::Arc},
    std::pair{std::string_view("spline"), HintPrimitive::Spline},
    std::pair{std::string_view("ellipse"), HintPrimitive::Ellipse},
};

constexpr std::array end_names{
    std::pair{std::string_view("start"), LineEnd::Start},
    std::pair{std::string_view("end"), LineEnd::End},
};

constexpr std::array symmetry_names{
    std::pair{std::string_view("mirror"), SymmetryKind::Mirror},
    std::pair{std::string_view("repeat"), SymmetryKind::Repeat},
};

// Reads the values of a hint file. The first problem is recorded; after
// it, readers return defaults and the caller's result is discarded.
class Reader {
public:
    [[nodiscard]] bool ok() const { return !error_; }

    [[nodiscard]] std::string error() const { return error_.value_or(""); }

    void fail(std::string_view where, std::string_view what) {
        if (!error_) {
            error_ = std::format("hints: {}: {}", where, what);
        }
    }

    // Checks that the value is an object with all required members and no
    // others than the optional ones.
    bool members(const JsonValue& value, std::string_view where, std::initializer_list<std::string_view> required,
                 std::initializer_list<std::string_view> optional = {}) {
        const core::JsonObject* object = value.object();
        if (object == nullptr) {
            fail(where, "object expected");
            return false;
        }
        for (const auto& [name, member] : *object) {
            if (std::ranges::find(required, name) == required.end() &&
                std::ranges::find(optional, name) == optional.end()) {
                fail(where, std::format("unknown member \"{}\"", name));
                return false;
            }
        }
        const auto* const missing =
            std::ranges::find_if(required, [&](std::string_view name) { return value.member(name) == nullptr; });
        if (missing != required.end()) {
            fail(where, std::format("member \"{}\" missing", *missing));
            return false;
        }
        return true;
    }

    std::int64_t integer(const JsonValue& value, std::string_view where, std::int64_t low, std::int64_t high) {
        const std::int64_t* number = value.integer();
        if (number == nullptr) {
            fail(where, "integer expected");
            return low;
        }
        if (*number < low || *number > high) {
            fail(where, std::format("{} not in {}..{}", *number, low, high));
            return low;
        }
        return *number;
    }

    // Printable ASCII of at most max_length characters; absent is empty.
    std::string text(const JsonValue* value, std::string_view where, std::size_t max_length = max_label) {
        if (value == nullptr) {
            return {};
        }
        const std::string* string = value->string();
        if (string == nullptr) {
            fail(where, "string expected");
            return {};
        }
        if (string->size() > max_length) {
            fail(where, "string too long");
            return {};
        }
        if (std::ranges::any_of(*string, [](char c) { return c < ' ' || c > '~'; })) {
            fail(where, "only printable ASCII is allowed");
            return {};
        }
        return *string;
    }

    std::string identifier(const JsonValue& value, std::string_view where) {
        std::string id = text(&value, where, max_id);
        const auto allowed = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; };
        if (ok() && (id.empty() || !std::ranges::all_of(id, allowed))) {
            fail(where, "ids are 1 to 32 characters of a-z, 0-9 and -");
        }
        return id;
    }

    // Absent is false.
    bool flag(const JsonValue* value, std::string_view where) {
        if (value == nullptr) {
            return false;
        }
        const bool* b = value->boolean();
        if (b == nullptr) {
            fail(where, "true or false expected");
            return false;
        }
        return *b;
    }

    // An array of min_size to max_size items; absent is empty.
    const JsonArray& array(const JsonValue* value, std::string_view where, std::size_t min_size, std::size_t max_size) {
        static const JsonArray empty;
        const JsonArray* items = value == nullptr ? &empty : value->array();
        if (items == nullptr) {
            fail(where, "array expected");
            return empty;
        }
        if (items->size() < min_size || items->size() > max_size) {
            fail(where, std::format("{} to {} items expected", min_size, max_size));
            return empty;
        }
        return *items;
    }

    template <typename Enum, std::size_t N>
    Enum choice(const JsonValue& value, std::string_view where,
                const std::array<std::pair<std::string_view, Enum>, N>& names) {
        const std::string* string = value.string();
        if (string != nullptr) {
            for (const auto& [name, e] : names) {
                if (*string == name) {
                    return e;
                }
            }
        }
        std::string expected;
        for (const auto& [name, e] : names) {
            expected += std::format("{}\"{}\"", expected.empty() ? "" : ", ", name);
        }
        fail(where, std::format("one of {} expected", expected));
        return names[0].second;
    }

    LineRef line_ref(const JsonValue& value, std::string_view where) {
        return static_cast<LineRef>(integer(value, where, 0, max_command));
    }

    core::Sha256Digest digest(const JsonValue& value, std::string_view where) {
        core::Sha256Digest digest{};
        const std::string* string = value.string();
        if (string == nullptr || string->size() != 2 * digest.size()) {
            fail(where, "64 lowercase hexadecimal digits expected");
            return digest;
        }
        for (std::size_t i = 0; i < string->size(); ++i) {
            const char c = (*string)[i];
            unsigned digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<unsigned>(c - 'a' + 10);
            } else {
                fail(where, "64 lowercase hexadecimal digits expected");
                return digest;
            }
            digest[i / 2] = static_cast<std::uint8_t>((static_cast<unsigned>(digest[i / 2]) << 4U) | digit);
        }
        return digest;
    }

private:
    std::optional<std::string> error_;
};

RegionHint read_region(Reader& in, const JsonValue& value, const std::string& where) {
    RegionHint region;
    if (!in.members(value, where, {"id", "at", "label"}, {"uncertain"})) {
        return region;
    }
    region.id = in.identifier(*value.member("id"), where + ".id");
    const JsonArray& at = in.array(value.member("at"), where + ".at", 2, 2);
    if (at.size() == 2) {
        region.x = static_cast<int>(in.integer(at[0], where + ".at", 0, picture_width - 1));
        region.y = static_cast<int>(in.integer(at[1], where + ".at", 0, picture_height - 1));
    }
    region.label = in.text(value.member("label"), where + ".label");
    region.uncertain = in.flag(value.member("uncertain"), where + ".uncertain");
    return region;
}

ChainHint read_chain(Reader& in, const JsonValue& value, const std::string& where) {
    ChainHint chain;
    if (!in.members(value, where, {"id", "lines", "primitive"}, {"label", "uncertain"})) {
        return chain;
    }
    chain.id = in.identifier(*value.member("id"), where + ".id");
    for (const JsonValue& item : in.array(value.member("lines"), where + ".lines", 2, max_chain_lines)) {
        const LineRef ref = in.line_ref(item, where + ".lines");
        if (in.ok() && std::ranges::find(chain.lines, ref) != chain.lines.end()) {
            in.fail(where + ".lines", "a line is listed twice");
        }
        chain.lines.push_back(ref);
    }
    chain.primitive = in.choice(*value.member("primitive"), where + ".primitive", primitive_names);
    chain.label = in.text(value.member("label"), where + ".label");
    chain.uncertain = in.flag(value.member("uncertain"), where + ".uncertain");
    return chain;
}

GapHint read_gap(Reader& in, const JsonValue& value, const std::string& where) {
    GapHint gap;
    if (!in.members(value, where, {"id", "from", "end", "to"}, {"label", "uncertain"})) {
        return gap;
    }
    gap.id = in.identifier(*value.member("id"), where + ".id");
    gap.from = in.line_ref(*value.member("from"), where + ".from");
    gap.end = in.choice(*value.member("end"), where + ".end", end_names);
    gap.to = in.line_ref(*value.member("to"), where + ".to");
    if (in.ok() && gap.from == gap.to) {
        in.fail(where, "a gap joins two different lines");
    }
    gap.label = in.text(value.member("label"), where + ".label");
    gap.uncertain = in.flag(value.member("uncertain"), where + ".uncertain");
    return gap;
}

SymmetryHint read_symmetry(Reader& in, const JsonValue& value, const std::string& where,
                           const std::vector<ChainHint>& chains) {
    SymmetryHint symmetry;
    if (!in.members(value, where, {"kind", "chains"}, {"note"})) {
        return symmetry;
    }
    symmetry.kind = in.choice(*value.member("kind"), where + ".kind", symmetry_names);
    for (const JsonValue& item : in.array(value.member("chains"), where + ".chains", 2, max_items)) {
        std::string id = in.identifier(item, where + ".chains");
        if (!in.ok()) {
            break;
        }
        if (std::ranges::none_of(chains, [&](const ChainHint& chain) { return chain.id == id; })) {
            in.fail(where + ".chains", std::format("no chain \"{}\"", id));
        } else if (std::ranges::find(symmetry.chains, id) != symmetry.chains.end()) {
            in.fail(where + ".chains", "a chain is listed twice");
        }
        symmetry.chains.push_back(std::move(id));
    }
    symmetry.note = in.text(value.member("note"), where + ".note");
    return symmetry;
}

template <typename T, typename Read>
std::vector<T> read_list(Reader& in, const JsonValue& file, std::string_view name, Read read) {
    std::vector<T> list;
    const JsonArray& items = in.array(file.member(name), name, 0, max_items);
    for (std::size_t i = 0; i < items.size() && in.ok(); ++i) {
        list.push_back(read(items[i], std::format("{}[{}]", name, i)));
    }
    return list;
}

// Ids are unique across the file, and no line is in two chains.
void check_unique(Reader& in, const PictureHints& hints) {
    std::vector<std::string> ids;
    ids.reserve(hints.regions.size() + hints.chains.size() + hints.gaps.size());
    for (const auto& region : hints.regions) {
        ids.push_back(region.id);
    }
    for (const auto& chain : hints.chains) {
        ids.push_back(chain.id);
    }
    for (const auto& gap : hints.gaps) {
        ids.push_back(gap.id);
    }
    std::ranges::stable_sort(ids);
    if (const auto repeated = std::ranges::adjacent_find(ids); repeated != ids.end()) {
        in.fail("ids", std::format("\"{}\" is used twice", *repeated));
    }
    std::vector<LineRef> chained;
    for (const auto& chain : hints.chains) {
        chained.insert(chained.end(), chain.lines.begin(), chain.lines.end());
    }
    std::ranges::stable_sort(chained);
    if (const auto repeated = std::ranges::adjacent_find(chained); repeated != chained.end()) {
        in.fail("chains", std::format("line {} is in two chains", *repeated));
    }
}

} // namespace

core::Result<PictureHints> parse_hints(std::string_view text) {
    auto parsed = core::parse_json(text);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    const JsonValue& file = *parsed;
    Reader in;
    PictureHints hints;
    if (in.members(file, "file", {"format", "version", "picture", "pic_sha256", "analyser"},
                   {"regions", "chains", "gaps", "symmetry"})) {
        if (in.text(file.member("format"), "format") != "sierra-hd-hints") {
            in.fail("format", "\"sierra-hd-hints\" expected");
        }
        in.integer(*file.member("version"), "version", hints_version, hints_version);
        hints.picture = static_cast<int>(in.integer(*file.member("picture"), "picture", 0, max_picture));
        hints.pic_sha256 = in.digest(*file.member("pic_sha256"), "pic_sha256");
        hints.analyser = in.text(file.member("analyser"), "analyser");
        const auto with = [&](auto read) {
            return [&in, read](const JsonValue& value, const std::string& where) { return read(in, value, where); };
        };
        hints.regions = read_list<RegionHint>(in, file, "regions", with(read_region));
        hints.chains = read_list<ChainHint>(in, file, "chains", with(read_chain));
        hints.gaps = read_list<GapHint>(in, file, "gaps", with(read_gap));
        hints.symmetry =
            read_list<SymmetryHint>(in, file, "symmetry", [&](const JsonValue& value, const std::string& where) {
                return read_symmetry(in, value, where, hints.chains);
            });
        check_unique(in, hints);
    }
    if (!in.ok()) {
        return core::make_error(core::ErrorCode::InvalidData, in.error());
    }
    return hints;
}

bool hints_match(const PictureHints& hints, std::span<const std::uint8_t> pic_data) {
    return core::sha256(pic_data) == hints.pic_sha256;
}

std::string hint_file_name(int picture) {
    return std::format("pic.{:03}.json", picture);
}

} // namespace sci::vector
