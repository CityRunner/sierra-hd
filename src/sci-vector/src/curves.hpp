// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finds the chains of the planar graph that imitate curves and replaces them
// by fitted curves (spec section 6.3). A chain runs between vertices where
// other edges meet it, or where it turns sharply; it keeps those vertices,
// so the graph's faces stay the same. A fit is used only if it stays within
// the tolerance of the chain and neither crosses nor encloses other
// geometry, and the fitted curve then bounds the faces on both sides.
#pragma once

#include "fitting.hpp"
#include "topology.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::vector::detail {

// Whether a line's edges may take part in chains, by the source index of
// its segment, and the chain hint it belongs to. Lines of a hint chain only
// with each other, and their chains may turn at any angle.
struct ChainLine {
    bool usable = false;
    std::int32_t hint = -1;
};

struct FittedChain {
    CurveKind kind = CurveKind::Line;
    std::vector<std::int32_t> edges; // Edge indices (half-edges 2i, 2i + 1), in chain order.
    // Per edge: the curve between its end vertices, in chain order, and the
    // half-edge the chain runs along.
    std::vector<std::vector<Vec2>> pieces;
    std::vector<std::int32_t> half_edges;
    std::int32_t hint = -1; // The chain hint it was fitted for.
};

struct CurveStats {
    int lines = 0;    // Chains straightened into one line.
    int arcs = 0;     // Chains fitted by circular arcs.
    int splines = 0;  // Chains fitted by cubic splines (possibly in pieces).
    int ellipses = 0; // Closed chains fitted by ellipses.
    int replaced_edges = 0;
    int rejected = 0;                       // Fits that would have changed the graph.
    std::vector<std::int32_t> failed_hints; // Hints whose fit failed or was rejected, once per chain.
};

// The picture coordinates of a vertex.
[[nodiscard]] Vec2 to_picture(const core::RationalPoint& p);

// Hint chains are fitted by the kind their hint asks for (hint_kinds, by
// hint index) within hint_tolerance; a hint chain that fails is left as it
// is and listed in the stats.
[[nodiscard]] std::vector<FittedChain> fit_chains(const Arrangement& arrangement, std::span<const ChainLine> lines,
                                                  double tolerance, std::span<const CurveKind> hint_kinds,
                                                  double hint_tolerance, CurveStats& stats);

} // namespace sci::vector::detail
