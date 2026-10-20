// SPDX-License-Identifier: GPL-3.0-or-later

#include "curves.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <utility>

namespace sci::vector::detail {

namespace {

// Chains break where they turn by more than 50 degrees: such corners are
// drawn, not curves.
constexpr double min_turn_cosine = 0.643;

struct Segment {
    Vec2 a;
    Vec2 b;
};

double orient(Vec2 a, Vec2 b, Vec2 c) {
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

// Whether the segments cross at a point inside both.
bool cross_properly(const Segment& s, const Segment& t) {
    const double o1 = orient(s.a, s.b, t.a);
    const double o2 = orient(s.a, s.b, t.b);
    const double o3 = orient(t.a, t.b, s.a);
    const double o4 = orient(t.a, t.b, s.b);
    return ((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) && ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0));
}

// Even-odd point in polygon; points on the boundary may count either way.
bool inside(Vec2 p, std::span<const Vec2> polygon) {
    bool in = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Vec2 a = polygon[i];
        const Vec2 b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + ((p.y - a.y) * (b.x - a.x) / (b.y - a.y))) {
            in = !in;
        }
    }
    return in;
}

struct Bounds {
    double left = 1e300;
    double top = 1e300;
    double right = -1e300;
    double bottom = -1e300;

    void add(Vec2 p) {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }

    [[nodiscard]] bool overlaps(const Segment& s) const {
        return std::max(s.a.x, s.b.x) >= left && std::min(s.a.x, s.b.x) <= right && std::max(s.a.y, s.b.y) >= top &&
               std::min(s.a.y, s.b.y) <= bottom;
    }

    [[nodiscard]] bool contains(Vec2 p) const { return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom; }
};

class ChainFinder {
public:
    ChainFinder(const Arrangement& arrangement, std::span<const ChainLine> lines)
        : arrangement_(&arrangement), incident_(arrangement.vertices.size()),
          qualifies_(arrangement.sources.size(), false) {
        for (std::size_t edge = 0; edge < arrangement.sources.size(); ++edge) {
            incident_[static_cast<std::size_t>(origin(static_cast<std::int32_t>(2 * edge)))].push_back(
                static_cast<std::int32_t>(edge));
            incident_[static_cast<std::size_t>(origin(static_cast<std::int32_t>((2 * edge) + 1)))].push_back(
                static_cast<std::int32_t>(edge));
            const std::int32_t source = arrangement.sources[edge];
            if (source >= 0 && static_cast<std::size_t>(source) < lines.size()) {
                const ChainLine& line = lines[static_cast<std::size_t>(source)];
                qualifies_[edge] = line.usable;
            }
        }
    }

    // The chain through the edge as half-edges in order, and whether it is
    // a closed loop of degree-two vertices.
    [[nodiscard]] std::pair<std::vector<std::int32_t>, bool> chain_through(std::int32_t edge) const {
        bool closed = false;
        const std::vector<std::int32_t> forward = walk(2 * edge, edge, closed);
        std::vector<std::int32_t> chain;
        if (!closed) {
            const std::vector<std::int32_t> backward = walk((2 * edge) + 1, edge, closed);
            for (const std::int32_t h : std::views::reverse(backward)) {
                chain.push_back(twin(h));
            }
        }
        chain.push_back(2 * edge);
        chain.insert(chain.end(), forward.begin(), forward.end());
        return {chain, closed};
    }

    [[nodiscard]] bool qualifies(std::size_t edge) const { return qualifies_[edge]; }

    // Whether other edges than the chain's two meet the vertex.
    [[nodiscard]] bool pinned(std::int32_t vertex) const {
        return incident_[static_cast<std::size_t>(vertex)].size() > 2;
    }

    [[nodiscard]] std::int32_t origin(std::int32_t half_edge) const {
        return arrangement_->half_edges[static_cast<std::size_t>(half_edge)].origin;
    }

    [[nodiscard]] std::int32_t twin(std::int32_t half_edge) const {
        return arrangement_->half_edges[static_cast<std::size_t>(half_edge)].twin;
    }

private:
    // The half-edge continuing the chain after h: the only other
    // qualifying edge at h's end vertex, if the chain does not turn sharply
    // there. Other edges may meet the vertex; it is then pinned.
    [[nodiscard]] std::optional<std::int32_t> continuation(std::int32_t h) const {
        const auto vertex = static_cast<std::size_t>(origin(twin(h)));
        const std::int32_t edge = h / 2;
        std::optional<std::int32_t> found;
        for (const std::int32_t candidate : incident_[vertex]) {
            if (candidate == edge || !qualifies_[static_cast<std::size_t>(candidate)]) {
                continue;
            }
            if (found) {
                return std::nullopt; // Branches.
            }
            found = candidate;
        }
        if (!found) {
            return std::nullopt;
        }
        const std::int32_t other = *found;
        const std::int32_t out = std::cmp_equal(origin(2 * other), vertex) ? 2 * other : (2 * other) + 1;
        const core::IntPoint d1 = arrangement_->half_edges[static_cast<std::size_t>(h)].direction;
        const core::IntPoint d2 = arrangement_->half_edges[static_cast<std::size_t>(out)].direction;
        const double dot = (static_cast<double>(d1.x) * static_cast<double>(d2.x)) +
                           (static_cast<double>(d1.y) * static_cast<double>(d2.y));
        const double lengths = std::sqrt(static_cast<double>((d1.x * d1.x) + (d1.y * d1.y)) *
                                         static_cast<double>((d2.x * d2.x) + (d2.y * d2.y)));
        if (dot < min_turn_cosine * lengths) {
            return std::nullopt;
        }
        return out;
    }

    [[nodiscard]] std::vector<std::int32_t> walk(std::int32_t h, std::int32_t start, bool& closed) const {
        std::vector<std::int32_t> chain;
        while (const auto next = continuation(h)) {
            if (*next / 2 == start) {
                closed = true;
                break;
            }
            chain.push_back(*next);
            h = *next;
        }
        return chain;
    }

    const Arrangement* arrangement_;
    std::vector<std::vector<std::int32_t>> incident_;
    std::vector<bool> qualifies_;
};

// Splits the curve at the points nearest to the chain's inner vertices, in
// order, into one piece per chain edge.
std::vector<std::vector<Vec2>> split_curve(const std::vector<Vec2>& curve, std::span<const Vec2> chain) {
    std::vector<std::vector<Vec2>> pieces;
    std::vector<Vec2> current{curve.front()};
    std::size_t segment = 0; // The curve segment the cut lies on.
    double cut = 0;          // Its parameter there.
    for (std::size_t k = 1; k + 1 < chain.size(); ++k) {
        const Vec2 target = chain[k];
        std::size_t best_segment = segment;
        double best_t = cut;
        double best = 1e300;
        for (std::size_t j = segment; j + 1 < curve.size(); ++j) {
            const Vec2 a = curve[j];
            const Vec2 d{.x = curve[j + 1].x - a.x, .y = curve[j + 1].y - a.y};
            const double length_squared = (d.x * d.x) + (d.y * d.y);
            double t = length_squared == 0 ? 0 : (((target.x - a.x) * d.x) + ((target.y - a.y) * d.y)) / length_squared;
            t = std::clamp(t, j == segment ? cut : 0.0, 1.0);
            const double dx = a.x + (d.x * t) - target.x;
            const double dy = a.y + (d.y * t) - target.y;
            const double distance = (dx * dx) + (dy * dy);
            if (distance < best) {
                best = distance;
                best_segment = j;
                best_t = t;
            }
        }
        for (std::size_t j = segment + 1; j <= best_segment; ++j) {
            if (current.back() != curve[j]) {
                current.push_back(curve[j]);
            }
        }
        const Vec2 a = curve[best_segment];
        const Vec2 b = curve[best_segment + 1];
        const Vec2 point{.x = a.x + ((b.x - a.x) * best_t), .y = a.y + ((b.y - a.y) * best_t)};
        if (current.back() != point) {
            current.push_back(point);
        }
        pieces.push_back(std::move(current));
        current = {point};
        segment = best_segment;
        cut = best_t;
    }
    for (std::size_t j = segment + 1; j < curve.size(); ++j) {
        if (current.back() != curve[j]) {
            current.push_back(curve[j]);
        }
    }
    pieces.push_back(std::move(current));
    return pieces;
}

// The graph's geometry in picture coordinates, as fits replace chains.
class Geometry {
public:
    explicit Geometry(const Arrangement& arrangement) : removed_(arrangement.sources.size(), false) {
        segments_.reserve(arrangement.sources.size());
        for (std::size_t edge = 0; edge < arrangement.sources.size(); ++edge) {
            const auto& from = arrangement.vertices[static_cast<std::size_t>(arrangement.half_edges[2 * edge].origin)];
            const auto& to =
                arrangement.vertices[static_cast<std::size_t>(arrangement.half_edges[(2 * edge) + 1].origin)];
            segments_.push_back(Segment{.a = to_picture(from), .b = to_picture(to)});
        }
    }

    // Whether replacing the chain (its points) by the curve keeps the graph:
    // the curve crosses no other edge, and no other vertex lies between the
    // chain and the curve.
    [[nodiscard]] bool allows(std::span<const std::int32_t> chain_edges, std::span<const Vec2> chain,
                              const std::vector<Vec2>& curve) const {
        Bounds box;
        for (const Vec2 p : chain) {
            box.add(p);
        }
        for (const Vec2 p : curve) {
            box.add(p);
        }
        std::vector<Vec2> region(chain.begin(), chain.end());
        region.insert(region.end(), curve.rbegin(), curve.rend());
        const auto in_chain = [&](std::size_t edge) {
            return std::ranges::find(chain_edges, static_cast<std::int32_t>(edge)) != chain_edges.end();
        };
        const auto blocks = [&](const Segment& other) {
            if (!box.overlaps(other)) {
                return false;
            }
            for (std::size_t i = 0; i + 1 < curve.size(); ++i) {
                if (cross_properly(Segment{.a = curve[i], .b = curve[i + 1]}, other)) {
                    return true;
                }
            }
            return std::ranges::any_of(std::array{other.a, other.b}, [&](Vec2 p) {
                return p != chain.front() && p != chain.back() && box.contains(p) && inside(p, region);
            });
        };
        for (std::size_t edge = 0; edge < segments_.size(); ++edge) {
            if (!removed_[edge] && !in_chain(edge) && blocks(segments_[edge])) {
                return false;
            }
        }
        return std::ranges::none_of(added_, blocks);
    }

    void replace(std::span<const std::int32_t> chain_edges, const std::vector<Vec2>& curve) {
        for (const std::int32_t edge : chain_edges) {
            removed_[static_cast<std::size_t>(edge)] = true;
        }
        for (std::size_t i = 0; i + 1 < curve.size(); ++i) {
            added_.push_back(Segment{.a = curve[i], .b = curve[i + 1]});
        }
    }

private:
    std::vector<Segment> segments_; // Per edge.
    std::vector<bool> removed_;
    std::vector<Segment> added_;
};

void count(CurveKind kind, CurveStats& stats) {
    switch (kind) {
    case CurveKind::Line:
        ++stats.lines;
        break;
    case CurveKind::Arc:
        ++stats.arcs;
        break;
    case CurveKind::Spline:
        ++stats.splines;
        break;
    case CurveKind::Ellipse:
        ++stats.ellipses;
        break;
    }
}

} // namespace

Vec2 to_picture(const core::RationalPoint& p) {
    const auto den = static_cast<double>(core::checked_mul(2, p.den()));
    return {.x = static_cast<double>(p.x_num()) / den, .y = static_cast<double>(p.y_num()) / den};
}

std::vector<FittedChain> fit_chains(const Arrangement& arrangement, std::span<const ChainLine> lines, double tolerance,
                                    CurveStats& stats) {
    const ChainFinder finder(arrangement, lines);
    Geometry geometry(arrangement);
    std::vector<bool> used(arrangement.sources.size(), false);
    std::vector<FittedChain> fitted;
    for (std::size_t edge = 0; edge < used.size(); ++edge) {
        if (used[edge] || !finder.qualifies(edge)) {
            continue;
        }
        const auto [half_edges, closed] = finder.chain_through(static_cast<std::int32_t>(edge));
        std::vector<std::int32_t> edges;
        std::vector<Vec2> points{
            to_picture(arrangement.vertices[static_cast<std::size_t>(finder.origin(half_edges[0]))]),
        };
        for (const std::int32_t h : half_edges) {
            used[static_cast<std::size_t>(h / 2)] = true;
            edges.push_back(h / 2);
            points.push_back(to_picture(arrangement.vertices[static_cast<std::size_t>(finder.origin(finder.twin(h)))]));
        }
        if (half_edges.size() < 2) {
            continue;
        }
        std::vector<bool> pinned(points.size(), true);
        for (std::size_t i = 1; i + 1 < points.size(); ++i) {
            pinned[i] = finder.pinned(finder.origin(half_edges[i]));
        }
        if (closed) {
            // A loop runs from and to its first vertex; pinned there only
            // if something meets it.
            const bool start_pinned = finder.pinned(finder.origin(half_edges[0]));
            pinned.front() = start_pinned;
            pinned.back() = start_pinned;
        }
        auto curve = fit_curve(points, pinned, tolerance);
        if (!curve) {
            continue;
        }
        if (!geometry.allows(edges, points, curve->points)) {
            ++stats.rejected;
            continue;
        }
        geometry.replace(edges, curve->points);
        count(curve->kind, stats);
        stats.replaced_edges += static_cast<int>(edges.size());
        fitted.push_back(FittedChain{
            .kind = curve->kind,
            .edges = std::move(edges),
            .pieces = split_curve(curve->points, points),
            .half_edges = half_edges,
        });
    }
    return fitted;
}

} // namespace sci::vector::detail
