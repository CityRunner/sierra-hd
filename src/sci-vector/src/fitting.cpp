// SPDX-License-Identifier: GPL-3.0-or-later

#include "fitting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace sci::vector::detail {

namespace {

// Curves are flattened into chords this long at most (in original pixels):
// at the largest scale, N = 10, a chord is 2.5 screen pixels, and its
// distance from a curve of radius 2 or more is below 0.05 pixels.
constexpr double max_chord = 0.25;

// Fixed-end-point fits split a chain at most this many times.
constexpr int max_split_depth = 4;

Vec2 operator+(Vec2 a, Vec2 b) {
    return {.x = a.x + b.x, .y = a.y + b.y};
}

Vec2 operator-(Vec2 a, Vec2 b) {
    return {.x = a.x - b.x, .y = a.y - b.y};
}

Vec2 operator*(Vec2 a, double s) {
    return {.x = a.x * s, .y = a.y * s};
}

double dot(Vec2 a, Vec2 b) {
    return (a.x * b.x) + (a.y * b.y);
}

double length(Vec2 a) {
    return std::sqrt(dot(a, a));
}

double distance_to_segment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    const double length_squared = dot(d, d);
    if (length_squared == 0) {
        return length(p - a);
    }
    const double t = std::clamp(dot(p - a, d) / length_squared, 0.0, 1.0);
    return length(p - (a + (d * t)));
}

double distance_to_polyline(Vec2 p, std::span<const Vec2> line) {
    if (line.size() == 1) {
        return length(p - line[0]);
    }
    double best = 1e300;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        best = std::min(best, distance_to_segment(p, line[i], line[i + 1]));
    }
    return best;
}

// Appends the points of the circular arc from a to b (both on the circle,
// less than half a turn apart), without a: halves of the arc are split at
// their middle until their chords are short.
void append_arc(Vec2 a, Vec2 b, Vec2 centre, double radius, std::vector<Vec2>& out) {
    std::vector<std::pair<Vec2, Vec2>> pending{{a, b}}; // Last first.
    while (!pending.empty()) {
        const auto [p, q] = pending.back();
        pending.pop_back();
        if (length(q - p) <= max_chord) {
            out.push_back(q);
            continue;
        }
        const Vec2 towards = ((p + q) * 0.5) - centre;
        const Vec2 middle = centre + (towards * (radius / length(towards)));
        pending.emplace_back(middle, q);
        pending.emplace_back(p, middle);
    }
}

// The straight line between the end points.
std::optional<FittedCurve> fit_line(std::span<const Vec2> chain) {
    return FittedCurve{.kind = CurveKind::Line, .points = {chain.front(), chain.back()}};
}

// The circular arc through both end points that fits the inner points
// best. Its centre lies on the chord's perpendicular bisector, at m + t n;
// for a point p, |p - c|^2 - r^2 = |p - m|^2 - |a - m|^2 - 2 t n.(p - m),
// which is linear in t, so the algebraic least-squares t has a closed form.
std::optional<FittedCurve> fit_arc(std::span<const Vec2> chain) {
    const Vec2 a = chain.front();
    const Vec2 b = chain.back();
    const Vec2 chord = b - a;
    const double chord_length = length(chord);
    if (chain.size() < 3 || chord_length == 0) {
        return std::nullopt;
    }
    const Vec2 m = (a + b) * 0.5;
    const Vec2 n{.x = -chord.y / chord_length, .y = chord.x / chord_length};
    const double half = dot(a - m, a - m);
    double sum_ab = 0;
    double sum_bb = 0;
    double side = 0;
    for (std::size_t i = 1; i + 1 < chain.size(); ++i) {
        const Vec2 p = chain[i] - m;
        const double alpha = dot(p, p) - half;
        const double beta = dot(n, p);
        sum_ab += alpha * beta;
        sum_bb += beta * beta;
        side += beta;
    }
    if (sum_bb == 0 || side == 0) {
        return std::nullopt;
    }
    const double t = sum_ab / (2 * sum_bb);
    const Vec2 centre = m + (n * t);
    const double radius = length(a - centre);
    const Vec2 middle = centre + (n * (side > 0 ? radius : -radius));
    FittedCurve curve{.kind = CurveKind::Arc, .points = {a}};
    append_arc(a, middle, centre, radius, curve.points);
    append_arc(middle, b, centre, radius, curve.points);
    curve.points.back() = b;
    return curve;
}

struct Cubic {
    std::array<Vec2, 4> p;

    [[nodiscard]] Vec2 at(double u) const {
        const double v = 1 - u;
        return (p[0] * (v * v * v)) + (p[1] * (3 * v * v * u)) + (p[2] * (3 * v * u * u)) + (p[3] * (u * u * u));
    }

    [[nodiscard]] Vec2 derivative(double u) const {
        const double v = 1 - u;
        return ((p[1] - p[0]) * (3 * v * v)) + ((p[2] - p[1]) * (6 * v * u)) + ((p[3] - p[2]) * (3 * u * u));
    }

    [[nodiscard]] Vec2 second_derivative(double u) const {
        return ((p[2] - (p[1] * 2) + p[0]) * (6 * (1 - u))) + ((p[3] - (p[2] * 2) + p[1]) * (6 * u));
    }
};

// The inner control points minimising the squared distances of the points
// at the given parameters, with the end points fixed.
std::optional<Cubic> solve_cubic(std::span<const Vec2> chain, std::span<const double> u) {
    const Vec2 a = chain.front();
    const Vec2 b = chain.back();
    double m11 = 0;
    double m12 = 0;
    double m22 = 0;
    Vec2 r1;
    Vec2 r2;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        const double t = u[i];
        const double s = 1 - t;
        const double b0 = s * s * s;
        const double b1 = 3 * s * s * t;
        const double b2 = 3 * s * t * t;
        const double b3 = t * t * t;
        const Vec2 rest = chain[i] - (a * b0) - (b * b3);
        m11 += b1 * b1;
        m12 += b1 * b2;
        m22 += b2 * b2;
        r1 = r1 + (rest * b1);
        r2 = r2 + (rest * b2);
    }
    const double det = (m11 * m22) - (m12 * m12);
    if (std::abs(det) < 1e-12) {
        return std::nullopt;
    }
    return Cubic{.p = {a, ((r1 * m22) - (r2 * m12)) * (1 / det), ((r2 * m11) - (r1 * m12)) * (1 / det), b}};
}

// A single cubic through both end points (Schneider's method: chord-length
// parameters, improved by Newton steps).
std::optional<FittedCurve> fit_cubic(std::span<const Vec2> chain) {
    if (chain.size() < 4) {
        return std::nullopt;
    }
    std::vector<double> u(chain.size(), 0);
    for (std::size_t i = 1; i < chain.size(); ++i) {
        u[i] = u[i - 1] + length(chain[i] - chain[i - 1]);
    }
    if (u.back() == 0) {
        return std::nullopt;
    }
    for (double& value : u) {
        value /= u.back();
    }
    std::optional<Cubic> cubic;
    for (int iteration = 0; iteration < 4; ++iteration) {
        cubic = solve_cubic(chain, u);
        if (!cubic) {
            return std::nullopt;
        }
        for (std::size_t i = 1; i + 1 < chain.size(); ++i) {
            const Vec2 error = cubic->at(u[i]) - chain[i];
            const Vec2 d1 = cubic->derivative(u[i]);
            const double denominator = dot(d1, d1) + dot(error, cubic->second_derivative(u[i]));
            if (denominator != 0) {
                u[i] = std::clamp(u[i] - (dot(error, d1) / denominator), 0.0, 1.0);
            }
        }
    }
    if (!cubic) {
        return std::nullopt;
    }
    const Cubic& fitted = *cubic;
    const double polygon =
        length(fitted.p[1] - fitted.p[0]) + length(fitted.p[2] - fitted.p[1]) + length(fitted.p[3] - fitted.p[2]);
    const auto steps = static_cast<int>(std::max(2.0, std::ceil(polygon / max_chord)));
    FittedCurve curve{.kind = CurveKind::Spline, .points = {chain.front()}};
    for (int step = 1; step < steps; ++step) {
        curve.points.push_back(fitted.at(static_cast<double>(step) / steps));
    }
    curve.points.push_back(chain.back());
    return curve;
}

bool within(const std::optional<FittedCurve>& curve, std::span<const Vec2> chain, double tolerance) {
    return curve && polyline_distance(curve->points, chain) <= tolerance;
}

// The best single primitive: a line, an arc or a cubic.
std::optional<FittedCurve> fit_piece(std::span<const Vec2> chain, double tolerance) {
    for (const auto fit : {fit_line, fit_arc, fit_cubic}) {
        if (auto curve = fit(chain); within(curve, chain, tolerance)) {
            return curve;
        }
    }
    return std::nullopt;
}

// Fits the chain, splitting it at its middle point where one piece does not
// fit, at most max_split_depth times. Pieces that fit nothing stay as they
// are.
FittedCurve fit_open(std::span<const Vec2> chain, double tolerance) {
    struct Part {
        std::size_t first = 0;
        std::size_t last = 0;
        int depth = 0;
    };

    FittedCurve result{.kind = CurveKind::Line, .points = {chain.front()}};
    int pieces = 0;
    std::vector<Part> pending{{.first = 0, .last = chain.size() - 1, .depth = 0}}; // Last first.
    while (!pending.empty()) {
        const Part part = pending.back();
        pending.pop_back();
        const std::span<const Vec2> piece = chain.subspan(part.first, part.last - part.first + 1);
        std::optional<FittedCurve> curve;
        if (piece.size() > 2) {
            curve = fit_piece(piece, tolerance);
        }
        if (curve) {
            result.points.insert(result.points.end(), curve->points.begin() + 1, curve->points.end());
            result.kind = curve->kind;
            ++pieces;
        } else if (piece.size() <= 2 || part.depth >= max_split_depth) {
            result.points.insert(result.points.end(), piece.begin() + 1, piece.end());
            ++pieces;
        } else {
            const std::size_t middle = part.first + (piece.size() / 2);
            pending.push_back({.first = middle, .last = part.last, .depth = part.depth + 1});
            pending.push_back({.first = part.first, .last = middle, .depth = part.depth + 1});
        }
    }
    if (pieces > 1) {
        result.kind = CurveKind::Spline;
    }
    return result;
}

// Solves the n x n system (row-major, augmented with the right-hand side)
// by Gaussian elimination with partial pivoting.
template <std::size_t N>
std::optional<std::array<double, N>> solve(std::array<std::array<double, N + 1>, N> m) {
    for (std::size_t column = 0; column < N; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < N; ++row) {
            if (std::abs(m[row][column]) > std::abs(m[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(m[pivot][column]) < 1e-12) {
            return std::nullopt;
        }
        std::swap(m[column], m[pivot]);
        for (std::size_t row = column + 1; row < N; ++row) {
            const double factor = m[row][column] / m[column][column];
            for (std::size_t k = column; k <= N; ++k) {
                m[row][k] -= factor * m[column][k];
            }
        }
    }
    std::array<double, N> x{};
    for (std::size_t row = N; row-- > 0;) {
        double sum = m[row][N];
        for (std::size_t k = row + 1; k < N; ++k) {
            sum -= m[row][k] * x[k];
        }
        x[row] = sum / m[row][row];
    }
    return x;
}

// Appends the points of the quarter of the unit circle from a to b, mapped
// by the ellipse's axes, without a (split as in append_arc).
void append_ellipse(Vec2 a, Vec2 b, Vec2 centre, Vec2 axes, std::vector<Vec2>& out) {
    const auto map = [&](Vec2 p) { return Vec2{.x = centre.x + (axes.x * p.x), .y = centre.y + (axes.y * p.y)}; };
    std::vector<std::pair<Vec2, Vec2>> pending{{a, b}}; // Last first.
    while (!pending.empty()) {
        const auto [p, q] = pending.back();
        pending.pop_back();
        if (length(map(q) - map(p)) <= max_chord) {
            out.push_back(map(q));
            continue;
        }
        const Vec2 middle = (p + q) * 0.5;
        const Vec2 on_circle = middle * (1 / length(middle));
        pending.emplace_back(on_circle, q);
        pending.emplace_back(p, on_circle);
    }
}

// An axis-aligned ellipse x^2 + B y^2 + C x + D y + E = 0 fitted to a
// closed chain (algebraic least squares).
std::optional<FittedCurve> fit_ellipse(std::span<const Vec2> chain) {
    std::array<std::array<double, 5>, 4> m{};
    double signed_area = 0;
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        const Vec2 p = chain[i];
        const std::array<double, 4> row{p.y * p.y, p.x, p.y, 1};
        const double target = -(p.x * p.x);
        for (std::size_t r = 0; r < 4; ++r) {
            for (std::size_t c = 0; c < 4; ++c) {
                m[r][c] += row[r] * row[c];
            }
            m[r][4] += row[r] * target;
        }
        signed_area += (p.x * chain[i + 1].y) - (chain[i + 1].x * p.y);
    }
    const auto solution = solve<4>(m);
    if (!solution || (*solution)[0] <= 0) {
        return std::nullopt;
    }
    const auto [b, c, d, e] = *solution;
    const Vec2 centre{.x = -c / 2, .y = -d / (2 * b)};
    const double k = (centre.x * centre.x) + (b * centre.y * centre.y) - e;
    if (k <= 0) {
        return std::nullopt;
    }
    const Vec2 axes{.x = std::sqrt(k), .y = std::sqrt(k / b)};
    // Start where the chain starts, and go round the same way.
    const Vec2 start{.x = (chain.front().x - centre.x) / axes.x, .y = (chain.front().y - centre.y) / axes.y};
    const double start_length = length(start);
    if (start_length == 0) {
        return std::nullopt;
    }
    const Vec2 q0 = start * (1 / start_length);
    const Vec2 q1 = signed_area > 0 ? Vec2{.x = -q0.y, .y = q0.x} : Vec2{.x = q0.y, .y = -q0.x};
    const std::array<Vec2, 5> quarters{q0, q1, q0 * -1, q1 * -1, q0};
    const Vec2 first{.x = centre.x + (axes.x * q0.x), .y = centre.y + (axes.y * q0.y)};
    FittedCurve curve{.kind = CurveKind::Ellipse, .points = {first}};
    for (std::size_t i = 0; i + 1 < quarters.size(); ++i) {
        append_ellipse(quarters[i], quarters[i + 1], centre, axes, curve.points);
    }
    curve.points.back() = curve.points.front();
    return curve;
}

// The unit tangent of the chain at point i: towards the next point at the
// start, from the previous one at the end, and along the chord between its
// neighbours elsewhere.
Vec2 tangent_at(std::span<const Vec2> chain, std::size_t i) {
    const Vec2 before = chain[i == 0 ? 0 : i - 1];
    const Vec2 after = chain[i + 1 == chain.size() ? i : i + 1];
    const Vec2 d = after - before;
    const double d_length = length(d);
    return d_length == 0 ? Vec2{} : d * (1 / d_length);
}

// A cubic from chain.front() leaving along t0 to chain.back() arriving
// along t3, with the distances of its inner control points fitted to the
// chain's inner points by least squares.
std::optional<FittedCurve> fit_tangent_cubic(std::span<const Vec2> chain, Vec2 t0, Vec2 t3) {
    const Vec2 a = chain.front();
    const Vec2 b = chain.back();
    const double chord = length(b - a);
    double alpha = chord / 3;
    double beta = chord / 3;
    if (chain.size() > 2) {
        std::vector<double> u(chain.size(), 0);
        for (std::size_t i = 1; i < chain.size(); ++i) {
            u[i] = u[i - 1] + length(chain[i] - chain[i - 1]);
        }
        double m11 = 0;
        double m12 = 0;
        double m22 = 0;
        double r1 = 0;
        double r2 = 0;
        for (std::size_t i = 1; i + 1 < chain.size(); ++i) {
            const double t = u[i] / u.back();
            const double s = 1 - t;
            const double b1 = 3 * s * s * t;
            const double b2 = 3 * s * t * t;
            const Vec2 rest = chain[i] - (a * ((s * s * s) + b1)) - (b * (b2 + (t * t * t)));
            m11 += b1 * b1;
            m12 -= b1 * b2 * dot(t0, t3);
            m22 += b2 * b2;
            r1 += b1 * dot(rest, t0);
            r2 -= b2 * dot(rest, t3);
        }
        const double det = (m11 * m22) - (m12 * m12);
        if (std::abs(det) > 1e-12) {
            alpha = ((r1 * m22) - (r2 * m12)) / det;
            beta = ((r2 * m11) - (r1 * m12)) / det;
        }
        if (alpha <= 0 || beta <= 0) {
            alpha = chord / 3;
            beta = chord / 3;
        }
    }
    const Cubic cubic{.p = {a, a + (t0 * alpha), b - (t3 * beta), b}};
    const double polygon = alpha + beta + length(cubic.p[2] - cubic.p[1]);
    const auto steps = static_cast<int>(std::max(2.0, std::ceil(polygon / max_chord)));
    FittedCurve curve{.kind = CurveKind::Spline, .points = {a}};
    for (int step = 1; step < steps; ++step) {
        curve.points.push_back(cubic.at(static_cast<double>(step) / steps));
    }
    curve.points.push_back(b);
    return curve;
}

// A chain pinned at inner points: one cubic per section between pins, with
// shared tangents at the pins, so the curve stays smooth across them.
// Sections that do not fit are fitted on their own.
FittedCurve fit_through_pins(std::span<const Vec2> chain, const std::vector<bool>& pinned, double tolerance) {
    FittedCurve curve{.kind = CurveKind::Spline, .points = {chain.front()}};
    std::size_t start = 0;
    for (std::size_t i = 1; i < chain.size(); ++i) {
        if (!pinned[i] && i + 1 != chain.size()) {
            continue;
        }
        const std::span<const Vec2> section = chain.subspan(start, i - start + 1);
        const std::optional<FittedCurve> cubic =
            fit_tangent_cubic(section, tangent_at(chain, start), tangent_at(chain, i));
        const FittedCurve piece =
            (cubic && polyline_distance(cubic->points, section) <= tolerance) ? *cubic : fit_open(section, tolerance);
        curve.points.insert(curve.points.end(), piece.points.begin() + 1, piece.points.end());
        start = i;
    }
    return curve;
}

// One section between pins fitted by the requested kind (not an ellipse),
// or nothing if that does not stay within the tolerance. A section of one
// edge stays straight.
std::optional<FittedCurve> fit_section_as(std::span<const Vec2> chain, std::size_t start, std::size_t end,
                                          CurveKind kind, double tolerance) {
    const std::span<const Vec2> section = chain.subspan(start, end - start + 1);
    if (section.size() == 2) {
        return fit_line(section);
    }
    std::optional<FittedCurve> curve;
    switch (kind) {
    case CurveKind::Line:
        curve = fit_line(section);
        break;
    case CurveKind::Arc:
        curve = fit_arc(section);
        break;
    case CurveKind::Spline:
        curve = fit_tangent_cubic(section, tangent_at(chain, start), tangent_at(chain, end));
        if (!within(curve, section, tolerance)) {
            curve = fit_open(section, tolerance);
        }
        break;
    case CurveKind::Ellipse:
        break;
    }
    if (!within(curve, section, tolerance)) {
        return std::nullopt;
    }
    return curve;
}

} // namespace

double polyline_distance(std::span<const Vec2> a, std::span<const Vec2> b) {
    double worst = 0;
    for (const Vec2 p : a) {
        worst = std::max(worst, distance_to_polyline(p, b));
    }
    for (const Vec2 p : b) {
        worst = std::max(worst, distance_to_polyline(p, a));
    }
    return worst;
}

std::optional<FittedCurve> fit_curve(std::span<const Vec2> chain, const std::vector<bool>& pinned, double tolerance) {
    if (chain.size() < 3) {
        return std::nullopt;
    }
    bool any_pinned = false;
    for (std::size_t i = 1; i + 1 < pinned.size(); ++i) {
        any_pinned = any_pinned || pinned[i];
    }
    FittedCurve curve;
    if (any_pinned) {
        curve = fit_through_pins(chain, pinned, tolerance);
    } else if (chain.front() == chain.back()) {
        std::optional<FittedCurve> ellipse;
        if (!pinned.front()) {
            ellipse = fit_ellipse(chain);
        }
        if (ellipse && polyline_distance(ellipse->points, chain) <= tolerance) {
            curve = std::move(*ellipse);
        } else {
            // Two open halves, both keeping the closing point.
            const std::size_t middle = chain.size() / 2;
            curve = fit_open(chain.first(middle + 1), tolerance);
            const FittedCurve second = fit_open(chain.subspan(middle), tolerance);
            curve.points.insert(curve.points.end(), second.points.begin() + 1, second.points.end());
            curve.kind = CurveKind::Spline;
        }
    } else {
        curve = fit_open(chain, tolerance);
    }
    // A fit made only of the chain's own edges replaces nothing.
    if (curve.points.size() == chain.size() && std::ranges::equal(curve.points, chain)) {
        return std::nullopt;
    }
    return curve;
}

std::optional<FittedCurve> fit_curve_as(std::span<const Vec2> chain, const std::vector<bool>& pinned, CurveKind kind,
                                        double tolerance) {
    if (chain.size() < 3) {
        return std::nullopt;
    }
    const bool closed = chain.front() == chain.back();
    bool any_pinned = false;
    for (std::size_t i = 1; i + 1 < pinned.size(); ++i) {
        any_pinned = any_pinned || pinned[i];
    }
    if (kind == CurveKind::Ellipse) {
        if (!closed || any_pinned || pinned.front()) {
            return std::nullopt;
        }
        auto ellipse = fit_ellipse(chain);
        if (!within(ellipse, chain, tolerance)) {
            return std::nullopt;
        }
        return ellipse;
    }
    // Sections between the pins; an unpinned loop is cut in two halves.
    std::vector<std::size_t> cuts{0};
    for (std::size_t i = 1; i + 1 < chain.size(); ++i) {
        if (pinned[i]) {
            cuts.push_back(i);
        }
    }
    if (closed && !any_pinned) {
        cuts.push_back(chain.size() / 2);
    }
    cuts.push_back(chain.size() - 1);
    FittedCurve curve{.kind = kind, .points = {chain.front()}};
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const auto piece = fit_section_as(chain, cuts[i], cuts[i + 1], kind, tolerance);
        if (!piece) {
            return std::nullopt;
        }
        curve.points.insert(curve.points.end(), piece->points.begin() + 1, piece->points.end());
    }
    if (curve.points.size() == chain.size() && std::ranges::equal(curve.points, chain)) {
        return std::nullopt;
    }
    return curve;
}

} // namespace sci::vector::detail
