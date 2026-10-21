// SPDX-License-Identifier: GPL-3.0-or-later

#include "fitting.hpp"
#include "sci_test.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using sci::vector::Vec2;
using sci::vector::detail::CurveKind;
using sci::vector::detail::fit_curve;
using sci::vector::detail::polyline_distance;

namespace {

// Points of an ellipse arc rounded to pixel centres, as a PIC would hold
// them. The angles come from a fixed table of cos/sin pairs (16 steps per
// turn), so the test needs no trigonometry.
constexpr std::size_t steps = 16;
constexpr std::array<Vec2, steps> unit{
    Vec2{.x = 1, .y = 0},
    Vec2{.x = 0.92388, .y = 0.38268},
    Vec2{.x = 0.70711, .y = 0.70711},
    Vec2{.x = 0.38268, .y = 0.92388},
    Vec2{.x = 0, .y = 1},
    Vec2{.x = -0.38268, .y = 0.92388},
    Vec2{.x = -0.70711, .y = 0.70711},
    Vec2{.x = -0.92388, .y = 0.38268},
    Vec2{.x = -1, .y = 0},
    Vec2{.x = -0.92388, .y = -0.38268},
    Vec2{.x = -0.70711, .y = -0.70711},
    Vec2{.x = -0.38268, .y = -0.92388},
    Vec2{.x = 0, .y = -1},
    Vec2{.x = 0.38268, .y = -0.92388},
    Vec2{.x = 0.70711, .y = -0.70711},
    Vec2{.x = 0.92388, .y = -0.38268},
};

std::vector<Vec2> ellipse_points(double rx, double ry, std::size_t first, std::size_t last) {
    std::vector<Vec2> points;
    for (std::size_t i = first; i <= last; ++i) {
        const Vec2 u = unit[i % steps];
        points.push_back({.x = std::floor(100 + (rx * u.x)) + 0.5, .y = std::floor(80 + (ry * u.y)) + 0.5});
    }
    return points;
}

} // namespace

SCI_TEST(near_straight_chains_become_lines) {
    const std::vector<Vec2> chain{
        {.x = 0.5, .y = 0.5},
        {.x = 4.5, .y = 0.5},
        {.x = 8.5, .y = 1.5},
        {.x = 12.5, .y = 1.5},
    };
    const auto curve = fit_curve(chain, std::vector<bool>(chain.size(), false), 0.5);
    SCI_CHECK(curve.has_value() && curve->kind == CurveKind::Line && curve->points.size() == 2);
}

SCI_TEST(arcs_keep_their_end_points_and_stay_close) {
    const std::vector<Vec2> chain = ellipse_points(30, 30, 0, 4);
    const auto curve = fit_curve(chain, std::vector<bool>(chain.size(), false), 0.5);
    SCI_CHECK(curve.has_value());
    if (curve) {
        SCI_CHECK(curve->kind == CurveKind::Arc || curve->kind == CurveKind::Spline);
        SCI_CHECK(curve->points.front() == chain.front() && curve->points.back() == chain.back());
        SCI_CHECK(polyline_distance(curve->points, chain) <= 0.5);
        SCI_CHECK(curve->points.size() > chain.size());
    }
}

SCI_TEST(free_closed_chains_become_ellipses) {
    std::vector<Vec2> chain = ellipse_points(20, 12, 0, steps);
    chain.back() = chain.front();
    const auto curve = fit_curve(chain, std::vector<bool>(chain.size(), false), 0.75);
    SCI_CHECK(curve.has_value() && curve->kind == CurveKind::Ellipse);
    if (curve) {
        SCI_CHECK(curve->points.front() == curve->points.back());
        SCI_CHECK(polyline_distance(curve->points, chain) <= 0.75);
    }
}

SCI_TEST(pinned_points_lie_on_the_curve) {
    const std::vector<Vec2> chain = ellipse_points(30, 20, 0, 6);
    std::vector<bool> pinned(chain.size(), false);
    pinned[3] = true;
    const auto curve = fit_curve(chain, pinned, 0.5);
    SCI_CHECK(curve.has_value());
    if (curve) {
        bool found = false;
        for (const Vec2 p : curve->points) {
            found = found || p == chain[3];
        }
        SCI_CHECK(found);
        SCI_CHECK(polyline_distance(curve->points, chain) <= 0.5);
    }
}

SCI_TEST(fits_never_leave_the_tolerance) {
    // A zig-zag cannot be smoothed within half a pixel: whatever is
    // returned stays within it.
    const std::vector<Vec2> chain{
        {.x = 0.5, .y = 0.5}, {.x = 3.5, .y = 3.5}, {.x = 6.5, .y = 0.5}, {.x = 9.5, .y = 3.5}, {.x = 12.5, .y = 0.5},
    };
    const auto curve = fit_curve(chain, std::vector<bool>(chain.size(), false), 0.5);
    SCI_CHECK(!curve || polyline_distance(curve->points, chain) <= 0.5);
}

SCI_TEST(forced_kinds_follow_the_hint) {
    using sci::vector::detail::fit_curve_as;
    // A shallow bend, forced into each kind.
    const std::vector<Vec2> chain{{.x = 0.5, .y = 0.5}, {.x = 20.5, .y = 1.5}, {.x = 40.5, .y = 0.5}};
    const std::vector<bool> pinned(chain.size(), false);
    const auto arc = fit_curve_as(chain, pinned, CurveKind::Arc, 1.0);
    SCI_CHECK(arc.has_value() && arc->kind == CurveKind::Arc && polyline_distance(arc->points, chain) <= 1.0);
    const auto line = fit_curve_as(chain, pinned, CurveKind::Line, 1.0);
    SCI_CHECK(line.has_value() && line->points.size() == 2);
    SCI_CHECK(!fit_curve_as(chain, pinned, CurveKind::Line, 0.5).has_value());
    SCI_CHECK(!fit_curve_as(chain, pinned, CurveKind::Ellipse, 1.0).has_value());
    // Pinned points stay on the curve.
    const std::vector<Vec2> longer{
        {.x = 0.5, .y = 0.5},  {.x = 10.5, .y = 1.5}, {.x = 20.5, .y = 1.5},
        {.x = 30.5, .y = 1.5}, {.x = 40.5, .y = 0.5},
    };
    const std::vector<bool> middle_pinned{true, false, true, false, true};
    const auto pinned_arc = fit_curve_as(longer, middle_pinned, CurveKind::Arc, 1.0);
    SCI_CHECK(pinned_arc.has_value() && std::ranges::find(pinned_arc->points, longer[2]) != pinned_arc->points.end());
}

SCI_TEST(forced_arcs_on_almost_straight_chains_stay_small) {
    // Nearly collinear: the best arc's radius is huge. Its points cannot be
    // computed reliably; the section is straight instead.
    const std::vector<Vec2> chain{{.x = 0.5, .y = 0.5}, {.x = 100.5, .y = 0.5 + 1e-9}, {.x = 200.5, .y = 0.5}};
    const std::vector<bool> pinned(chain.size(), false);
    const auto curve = sci::vector::detail::fit_curve_as(chain, pinned, CurveKind::Arc, 1.0);
    SCI_CHECK(curve.has_value() && curve->points.size() < 10000);
}
