// SPDX-License-Identifier: GPL-3.0-or-later

#include "topology.hpp"

#include "sci/core/rounding.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <numeric>
#include <utility>

namespace sci::vector::detail {

namespace {

using core::IntPoint;
using core::RationalPoint;

// The direction with gcd 1 that points towards increasing (x, y) order, so
// that walking along it visits points in RationalPoint order.
IntPoint primitive_direction(IntPoint d) {
    const std::int64_t divisor = std::gcd(d.x, d.y);
    SCI_ASSERT(divisor > 0);
    IntPoint result{.x = d.x / divisor, .y = d.y / divisor};
    if (result.x < 0 || (result.x == 0 && result.y < 0)) {
        result = IntPoint{.x = -result.x, .y = -result.y};
    }
    return result;
}

bool in_range(IntPoint p) {
    return std::abs(p.x) <= max_coordinate && std::abs(p.y) <= max_coordinate;
}

// The point a + (numerator / denominator) * direction.
RationalPoint point_along(IntPoint a, IntPoint direction, std::int64_t numerator, std::int64_t denominator) {
    return {core::checked_add(core::checked_mul(a.x, denominator), core::checked_mul(numerator, direction.x)),
            core::checked_add(core::checked_mul(a.y, denominator), core::checked_mul(numerator, direction.y)),
            denominator};
}

// A segment ready for intersection tests: its end points in order and an
// integer bounding box that contains it.
struct Prepared {
    IntPoint a;
    IntPoint direction; // b - a
    RationalPoint low;
    RationalPoint high;
    std::int64_t min_x = 0;
    std::int64_t max_x = 0;
    std::int64_t min_y = 0;
    std::int64_t max_y = 0;
};

Prepared prepare(const TopoSegment& segment) {
    SCI_ASSERT(segment.a != segment.b && in_range(segment.a) && in_range(segment.b));
    SCI_ASSERT(core::orientation(segment.a, segment.b, segment.from) == 0);
    SCI_ASSERT(core::orientation(segment.a, segment.b, segment.to) == 0);
    Prepared prepared{
        .a = segment.a,
        .direction = segment.b - segment.a,
        .low = std::min(segment.from, segment.to),
        .high = std::max(segment.from, segment.to),
    };
    SCI_ASSERT(prepared.low != prepared.high);
    const auto [min_x, max_x] = std::minmax(prepared.low.x(), prepared.high.x());
    const auto [min_y, max_y] = std::minmax(prepared.low.y(), prepared.high.y());
    prepared.min_x = core::floor_div(min_x.num(), min_x.den());
    prepared.max_x = core::ceil_div(max_x.num(), max_x.den());
    prepared.min_y = core::floor_div(min_y.num(), min_y.den());
    prepared.max_y = core::ceil_div(max_y.num(), max_y.den());
    return prepared;
}

bool boxes_overlap(const Prepared& s, const Prepared& t) {
    return s.min_x <= t.max_x && t.min_x <= s.max_x && s.min_y <= t.max_y && t.min_y <= s.max_y;
}

bool contains(const Prepared& s, const RationalPoint& p) {
    return !(p < s.low) && !(s.high < p);
}

// Adds the points where s and t meet to their split lists.
void intersect(const Prepared& s, const Prepared& t, std::vector<RationalPoint>& s_splits,
               std::vector<RationalPoint>& t_splits) {
    const IntPoint offset = t.a - s.a;
    const std::int64_t denominator = core::cross(s.direction, t.direction);
    if (denominator == 0) {
        if (core::cross(offset, s.direction) != 0) {
            return; // Parallel, apart.
        }
        // Collinear: each end point inside the other segment splits it.
        for (const RationalPoint& p : {t.low, t.high}) {
            if (contains(s, p)) {
                s_splits.push_back(p);
            }
        }
        for (const RationalPoint& p : {s.low, s.high}) {
            if (contains(t, p)) {
                t_splits.push_back(p);
            }
        }
        return;
    }
    // The point on both lines.
    const RationalPoint p = point_along(s.a, s.direction, core::cross(offset, t.direction), denominator);
    if (contains(s, p) && contains(t, p)) {
        s_splits.push_back(p);
        t_splits.push_back(p);
    }
}

struct Edge {
    std::int32_t from = 0; // Vertex indices, from < to in point order.
    std::int32_t to = 0;
    IntPoint direction; // Primitive, from -> to.
    std::int32_t source = no_source;
};

// Directions sorted clockwise on screen (y downwards), starting at +x.
int half_plane(IntPoint d) {
    return (d.y > 0 || (d.y == 0 && d.x > 0)) ? 0 : 1;
}

bool clockwise_before(IntPoint a, IntPoint b) {
    if (half_plane(a) != half_plane(b)) {
        return half_plane(a) < half_plane(b);
    }
    return core::cross(a, b) > 0;
}

std::vector<Edge> split_into_edges(std::span<const TopoSegment> segments, std::vector<RationalPoint>& vertices) {
    std::vector<Prepared> prepared;
    prepared.reserve(segments.size());
    for (const TopoSegment& segment : segments) {
        prepared.push_back(prepare(segment));
    }
    std::vector<std::vector<RationalPoint>> splits(segments.size());
    for (std::size_t i = 0; i < prepared.size(); ++i) {
        splits[i].push_back(prepared[i].low);
        splits[i].push_back(prepared[i].high);
        for (std::size_t j = i + 1; j < prepared.size(); ++j) {
            if (boxes_overlap(prepared[i], prepared[j])) {
                intersect(prepared[i], prepared[j], splits[i], splits[j]);
            }
        }
    }
    for (auto& list : splits) {
        std::ranges::stable_sort(list);
        const auto duplicates = std::ranges::unique(list);
        list.erase(duplicates.begin(), duplicates.end());
        vertices.insert(vertices.end(), list.begin(), list.end());
    }
    std::ranges::stable_sort(vertices);
    const auto duplicates = std::ranges::unique(vertices);
    vertices.erase(duplicates.begin(), duplicates.end());

    const auto vertex_index = [&](const RationalPoint& p) {
        return static_cast<std::int32_t>(std::ranges::lower_bound(vertices, p) - vertices.begin());
    };
    std::vector<Edge> edges;
    for (std::size_t i = 0; i < splits.size(); ++i) {
        const IntPoint direction = primitive_direction(prepared[i].direction);
        for (std::size_t k = 0; k + 1 < splits[i].size(); ++k) {
            edges.push_back(Edge{
                .from = vertex_index(splits[i][k]),
                .to = vertex_index(splits[i][k + 1]),
                .direction = direction,
                .source = segments[i].source,
            });
        }
    }
    // Edges between the same vertices lie on the same line: keep one.
    std::ranges::stable_sort(edges, {}, [](const Edge& e) { return std::pair{e.from, e.to}; });
    std::vector<Edge> unique;
    for (const Edge& edge : edges) {
        if (!unique.empty() && unique.back().from == edge.from && unique.back().to == edge.to) {
            unique.back().source = shared_source;
        } else {
            unique.push_back(edge);
        }
    }
    return unique;
}

} // namespace

TopoSegment make_segment(IntPoint a, IntPoint b, std::int32_t source) {
    return TopoSegment{.a = a, .b = b, .from = RationalPoint(a), .to = RationalPoint(b), .source = source};
}

bool segments_touch(const TopoSegment& s, const TopoSegment& t) {
    std::vector<RationalPoint> s_splits;
    std::vector<RationalPoint> t_splits;
    intersect(prepare(s), prepare(t), s_splits, t_splits);
    return !s_splits.empty() || !t_splits.empty();
}

std::optional<RationalPoint> line_meets_segment(IntPoint a, IntPoint b, const TopoSegment& segment) {
    const Prepared t = prepare(segment);
    const IntPoint direction = b - a;
    const std::int64_t denominator = core::cross(direction, t.direction);
    if (denominator == 0) {
        return std::nullopt;
    }
    const RationalPoint p = point_along(a, direction, core::cross(t.a - a, t.direction), denominator);
    if (!contains(t, p)) {
        return std::nullopt;
    }
    return p;
}

std::optional<RationalPoint> perpendicular_foot(IntPoint p, const TopoSegment& segment) {
    const Prepared t = prepare(segment);
    // The foot is t.a + (dot(p - t.a, d) / dot(d, d)) * d.
    const RationalPoint foot =
        point_along(t.a, t.direction, core::dot(p - t.a, t.direction), core::dot(t.direction, t.direction));
    if (!contains(t, foot)) {
        return std::nullopt;
    }
    return foot;
}

Arrangement build_arrangement(std::span<const TopoSegment> segments) {
    Arrangement arrangement;
    const std::vector<Edge> edges = split_into_edges(segments, arrangement.vertices);

    auto& half_edges = arrangement.half_edges;
    half_edges.reserve(2 * edges.size());
    arrangement.sources.reserve(edges.size());
    for (const Edge& edge : edges) {
        arrangement.sources.push_back(edge.source);
        const auto index = static_cast<std::int32_t>(half_edges.size());
        half_edges.push_back(HalfEdge{.origin = edge.from, .twin = index + 1, .direction = edge.direction});
        half_edges.push_back(HalfEdge{
            .origin = edge.to,
            .twin = index,
            .direction = IntPoint{.x = -edge.direction.x, .y = -edge.direction.y},
        });
    }

    // The half-edges leaving each vertex, clockwise.
    std::vector<std::vector<std::int32_t>> outgoing(arrangement.vertices.size());
    for (std::size_t i = 0; i < half_edges.size(); ++i) {
        outgoing[static_cast<std::size_t>(half_edges[i].origin)].push_back(static_cast<std::int32_t>(i));
    }
    for (auto& list : outgoing) {
        std::ranges::stable_sort(list, [&](std::int32_t a, std::int32_t b) {
            return clockwise_before(half_edges[static_cast<std::size_t>(a)].direction,
                                    half_edges[static_cast<std::size_t>(b)].direction);
        });
    }
    // Turning from the twin to the counter-clockwise neighbour keeps the
    // face on the right of every half-edge.
    for (auto& list : outgoing) {
        for (std::size_t k = 0; k < list.size(); ++k) {
            const std::int32_t leaving = list[k];
            const std::int32_t previous = list[(k + list.size() - 1) % list.size()];
            half_edges[static_cast<std::size_t>(half_edges[static_cast<std::size_t>(leaving)].twin)].next = previous;
        }
    }

    std::vector<bool> assigned(half_edges.size(), false);
    for (std::size_t start = 0; start < half_edges.size(); ++start) {
        if (assigned[start]) {
            continue;
        }
        const std::int32_t face = arrangement.face_count++;
        auto current = static_cast<std::int32_t>(start);
        do {
            const auto index = static_cast<std::size_t>(current);
            assigned[index] = true;
            half_edges[index].face = face;
            current = half_edges[index].next;
        } while (std::cmp_not_equal(current, start));
    }
    return arrangement;
}

namespace {

// A non-horizontal edge for scanning: from its top vertex downwards.
struct Crossing {
    RationalPoint top;
    RationalPoint bottom;
    IntPoint direction; // Downwards (y > 0).
    std::int32_t face_after = 0;
};

// The x of the crossing at row y (doubled), as num / den with den > 0.
std::pair<std::int64_t, std::int64_t> crossing_x(const Crossing& edge, std::int64_t y) {
    const std::int64_t den = edge.top.den();
    const std::int64_t rise = core::checked_sub(core::checked_mul(y, den), edge.top.y_num());
    const std::int64_t num = core::checked_add(core::checked_mul(edge.top.x_num(), edge.direction.y),
                                               core::checked_mul(rise, edge.direction.x));
    return {num, core::checked_mul(den, edge.direction.y)};
}

// The arrangement's edges for scanning: non-horizontal ones as crossings,
// horizontal ones as (left, right) pairs.
struct ScanSets {
    std::vector<Crossing> crossings;
    std::vector<std::pair<RationalPoint, RationalPoint>> horizontal;
};

ScanSets scan_sets(const Arrangement& arrangement) {
    ScanSets sets;
    for (std::size_t i = 0; i < arrangement.half_edges.size(); i += 2) {
        const HalfEdge& forward = arrangement.half_edges[i];
        const HalfEdge& backward = arrangement.half_edges[i + 1];
        if (forward.direction.y == 0) {
            // Edges go from the smaller point to the larger: left to right.
            sets.horizontal.emplace_back(arrangement.vertices[static_cast<std::size_t>(forward.origin)],
                                         arrangement.vertices[static_cast<std::size_t>(backward.origin)]);
            continue;
        }
        // The face after the edge is the one of the half-edge going up.
        const bool forward_down = forward.direction.y > 0;
        const HalfEdge& down = forward_down ? forward : backward;
        const HalfEdge& up = forward_down ? backward : forward;
        sets.crossings.push_back(Crossing{
            .top = arrangement.vertices[static_cast<std::size_t>(down.origin)],
            .bottom = arrangement.vertices[static_cast<std::size_t>(up.origin)],
            .direction = down.direction,
            .face_after = up.face,
        });
    }
    return sets;
}

struct Hit {
    double x = 0;
    std::int64_t num = 0;
    std::int64_t den = 1;
    const Crossing* edge = nullptr;
};

// The crossings of row y (doubled) in order of x; edges through the same
// point by direction, so that the edge closest to +x comes last and decides
// what follows.
void row_hits(const std::vector<Crossing>& crossings, std::int64_t y, std::vector<Hit>& hits) {
    hits.clear();
    for (const Crossing& edge : crossings) {
        // top.y <= y < bottom.y
        if (edge.top.y_num() <= core::checked_mul(y, edge.top.den()) &&
            core::checked_mul(y, edge.bottom.den()) < edge.bottom.y_num()) {
            const auto [num, den] = crossing_x(edge, y);
            hits.push_back(Hit{
                .x = static_cast<double>(num) / static_cast<double>(den),
                .num = num,
                .den = den,
                .edge = &edge,
            });
        }
    }
    std::ranges::stable_sort(hits, [](const Hit& a, const Hit& b) {
        if (a.x != b.x) {
            return a.x < b.x;
        }
        return core::cross(a.edge->direction, b.edge->direction) < 0;
    });
}

// Labels the pixel centres (2 * column + 1, y) of one row from its hits.
void label_row(const std::vector<Hit>& hits, std::span<std::int32_t> row) {
    std::size_t next = 0;
    std::int32_t face = outside;
    for (std::size_t column = 0; column < row.size(); ++column) {
        const auto x = static_cast<std::int64_t>((2 * column) + 1);
        bool touching = false;
        while (next < hits.size()) {
            const std::int64_t scaled = core::checked_mul(x, hits[next].den);
            touching = hits[next].num == scaled;
            if (hits[next].num >= scaled) {
                break;
            }
            face = hits[next].edge->face_after;
            ++next;
        }
        row[column] = touching ? on_edge : face;
    }
}

// Marks the pixel centres of row y (doubled) that lie on horizontal edges.
void mark_horizontal(const std::vector<std::pair<RationalPoint, RationalPoint>>& horizontal, std::int64_t y,
                     std::span<std::int32_t> row) {
    const auto width = static_cast<std::int64_t>(row.size());
    for (const auto& [left, right] : horizontal) {
        if (left.y_num() != core::checked_mul(y, left.den())) {
            continue;
        }
        const std::int64_t first = std::max<std::int64_t>(0, core::ceil_div(left.x_num(), left.den()));
        const std::int64_t last = std::min<std::int64_t>(2 * width, right.x_num() / right.den());
        for (std::int64_t x = first; x <= last; ++x) {
            const RationalPoint centre(IntPoint{.x = x, .y = y});
            if (x % 2 != 0 && !(centre < left) && !(right < centre)) {
                row[static_cast<std::size_t>(x / 2)] = on_edge;
            }
        }
    }
}

} // namespace

std::vector<std::int32_t> label_pixel_centres(const Arrangement& arrangement, int width, int height) {
    const ScanSets sets = scan_sets(arrangement);
    const auto row_size = static_cast<std::size_t>(width);
    std::vector<std::int32_t> labels(row_size * static_cast<std::size_t>(height), outside);
    std::vector<Hit> hits;
    for (int row = 0; row < height; ++row) {
        const std::int64_t y = (2 * std::int64_t{row}) + 1;
        const std::span<std::int32_t> labels_row =
            std::span(labels).subspan(static_cast<std::size_t>(row) * row_size, row_size);
        row_hits(sets.crossings, y, hits);
        label_row(hits, labels_row);
        mark_horizontal(sets.horizontal, y, labels_row);
    }
    return labels;
}

} // namespace sci::vector::detail
