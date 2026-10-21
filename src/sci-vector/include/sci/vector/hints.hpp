// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scene-analysis hints (spec section 6.7): per picture, which chains of PIC
// lines form one curve and of what kind, which gaps the artist meant to
// close, labels for regions, and what is in front of or contains what. The
// analysis is done offline; its results are committed as
// hints/pic.NNN.json and frozen at the milestone 3b gate
// (docs/SPEC_ADDENDA.md, A1).
// The format is described in hints/README.md and hints/schema.json.
//
// Hints are untrusted input and only advice: the vector analysis applies a
// hint only where it keeps the acceptance criteria (spec section 6.4) and
// otherwise falls back to the automatic result. Hints refer to lines by
// their PIC command, never by coordinates, so they hold for mirrored
// drawings too.
#pragma once

#include "sci/core/error.hpp"
#include "sci/core/sha256.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sci::vector {

// A line of the picture: the index of its command in the picture's command
// stream as sci-pic parses it (one line per command).
using LineRef = std::uint32_t;

enum class HintPrimitive : std::int8_t { Line, Arc, Spline, Ellipse };

// Lines that together imitate one curve of the given kind.
struct ChainHint {
    std::string id;
    std::vector<LineRef> lines;
    HintPrimitive primitive = HintPrimitive::Spline;
    std::string label;
    bool uncertain = false;
};

enum class LineEnd : std::int8_t { Start, End };

// A gap the artist meant to close: from one end of a line to the nearest
// point of another line.
struct GapHint {
    std::string id;
    LineRef from = 0;
    LineEnd end = LineEnd::Start;
    LineRef to = 0;
    std::string label;
    bool uncertain = false;
};

// What a region shows; informational. `x`, `y` is a pixel of the region in
// picture coordinates as stored (before mirroring).
struct RegionHint {
    std::string id;
    int x = 0;
    int y = 0;
    std::string label;
    bool uncertain = false;
};

enum class SymmetryKind : std::int8_t { Mirror, Repeat };

// Chains that mirror or repeat each other; informational.
struct SymmetryHint {
    SymmetryKind kind = SymmetryKind::Mirror;
    std::vector<std::string> chains; // Chain ids.
    std::string note;
};

enum class RelationKind : std::int8_t { InFrontOf, Behind, Contains };

// Depth or containment between two regions or chains, by id: `a` is in
// front of, behind, or contains `b` (spec section 6.10, addendum A1). Used
// by the vector path from milestone 3b.
struct RelationHint {
    RelationKind kind = RelationKind::InFrontOf;
    std::string a;
    std::string b;
    std::string note;
    bool uncertain = false;
};

struct PictureHints {
    int picture = 0;                 // PIC resource number.
    core::Sha256Digest pic_sha256{}; // Of the PIC resource data the hints were made for.
    std::string analyser;            // Who or what made the hints.
    std::vector<RegionHint> regions;
    std::vector<ChainHint> chains;
    std::vector<GapHint> gaps;
    std::vector<SymmetryHint> symmetry;
    std::vector<RelationHint> relations;
};

inline constexpr int hints_version = 1;

// Parses and validates a hint file. Every member is checked; unknown
// members are an error, so that typos do not pass silently.
[[nodiscard]] core::Result<PictureHints> parse_hints(std::string_view text);

// Whether the hints were made for this PIC resource (its decompressed data).
[[nodiscard]] bool hints_match(const PictureHints& hints, std::span<const std::uint8_t> pic_data);

// The hint file name of a picture: "pic.NNN.json".
[[nodiscard]] std::string hint_file_name(int picture);

} // namespace sci::vector
