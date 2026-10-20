// SPDX-License-Identifier: GPL-3.0-or-later
//
// Recognises chains of short PIC lines that imitate curves (spec section
// 6.3): least-squares fits of a line, a circular arc, a cubic spline or,
// for closed chains, an axis-aligned ellipse, in double precision with
// only +, -, *, / and sqrt (spec section 5.3). Every fit keeps the chain's
// end points, so the curve still meets the lines at its ends.
#pragma once

#include "sci/vector/scene.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sci::vector::detail {

enum class CurveKind : std::int8_t { Line, Arc, Spline, Ellipse };

struct FittedCurve {
    CurveKind kind = CurveKind::Line;
    // The curve as a polyline from the chain's first point to its last,
    // with segments short enough to look smooth at any scale.
    std::vector<Vec2> points;
};

// The largest distance, in either direction, between two polylines.
[[nodiscard]] double polyline_distance(std::span<const Vec2> a, std::span<const Vec2> b);

// Fits a curve to the chain's points, keeping its first and last point and
// the pinned points (where other edges meet the chain), that stays within
// `tolerance` of the chain (both ways). A chain whose first and last point
// coincide is closed; if nothing pins it, it may become an ellipse.
[[nodiscard]] std::optional<FittedCurve> fit_curve(std::span<const Vec2> chain, const std::vector<bool>& pinned,
                                                   double tolerance);

} // namespace sci::vector::detail
