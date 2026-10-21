// SPDX-License-Identifier: GPL-3.0-or-later

#include "stroke_integrity.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>

namespace sci::vector::detail {

namespace {

// A line end reaches the contour it ends on if the contour is this close
// across (original pixels): a pixel and a half covers every way a 1x end
// pixel can touch a line. Along the line's direction it may move at most
// max_end_move; at a shallower angle the line runs along the contour.
constexpr double max_across = 1.5;
constexpr double max_end_move = 3.0;

// Ends closer than this already join (their discs overlap by a quarter).
constexpr double joined = 0.75;

// Pixels of a hidden run this far apart along the line belong to one run.
constexpr double run_gap = 1.5;

constexpr const char* joint_evidence = "1x: the line ends are on touching pixels and all are seen";
constexpr const char* contact_evidence = "1x: the line end touches the contour, which is seen there";
constexpr const char* near_end_evidence = "1x: the line end touches the other line next to its end";
constexpr const char* side_evidence = "1x: the line end lies beside the contour, which is seen there";
constexpr const char* hidden_evidence = "1x: another object is seen on these pixels of the line";
constexpr const char* no_crossing_evidence = "kept: the line does not reach the contour within 1.5 pixels across";
constexpr const char* passes_evidence = "kept: the line crosses the contour's edge elsewhere";
constexpr const char* on_contour_evidence = "kept: the end pixel lies on the contour's stroke";

Vec2 operator-(Vec2 a, Vec2 b) {
    return {.x = a.x - b.x, .y = a.y - b.y};
}

Vec2 operator+(Vec2 a, Vec2 b) {
    return {.x = a.x + b.x, .y = a.y + b.y};
}

Vec2 operator*(Vec2 a, double s) {
    return {.x = a.x * s, .y = a.y * s};
}

double dot(Vec2 a, Vec2 b) {
    return (a.x * b.x) + (a.y * b.y);
}

double cross(Vec2 a, Vec2 b) {
    return (a.x * b.y) - (a.y * b.x);
}

double length(Vec2 a) {
    return std::sqrt(dot(a, a));
}

int pixel_x(std::size_t pixel) {
    return static_cast<int>(pixel % picture_width);
}

int pixel_y(std::size_t pixel) {
    return static_cast<int>(pixel / picture_width);
}

Vec2 centre_of(std::size_t pixel) {
    return {.x = pixel_x(pixel) + 0.5, .y = pixel_y(pixel) + 0.5};
}

// Whether two pixels are different and touch, among the eight neighbours.
bool touching(std::size_t a, std::size_t b) {
    const int dx = std::abs(pixel_x(a) - pixel_x(b));
    const int dy = std::abs(pixel_y(a) - pixel_y(b));
    return a != b && dx <= 1 && dy <= 1;
}

// A line during the pass: its 1x data and its strokes (one at first; runs
// removed under other lines split it).
struct Line {
    std::int32_t primitive = -1;
    std::vector<std::size_t> path;
    std::vector<Stroke> strokes;
    std::vector<Stroke> connectors; // Added to join other lines; not the line's own geometry.

    [[nodiscard]] std::size_t first() const { return path.front(); }

    [[nodiscard]] std::size_t last() const { return path.back(); }

    [[nodiscard]] bool is_end(std::size_t pixel) const { return pixel == first() || pixel == last(); }
};

// The closest point of a polyline to p: its arc-length position and the
// distance.
std::pair<double, double> closest(const std::vector<Vec2>& points, Vec2 p) {
    double best = 1e300;
    double best_s = 0;
    double s = 0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec2 d = points[i + 1] - points[i];
        const double length_squared = dot(d, d);
        const double t = length_squared == 0 ? 0 : std::clamp(dot(p - points[i], d) / length_squared, 0.0, 1.0);
        const double distance = length(p - (points[i] + (d * t)));
        if (distance < best) {
            best = distance;
            best_s = s + (t * std::sqrt(length_squared));
        }
        s += std::sqrt(length_squared);
    }
    if (points.size() == 1) {
        best = length(p - points[0]);
    }
    return {best_s, best};
}

double total_length(const std::vector<Vec2>& points) {
    double s = 0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        s += length(points[i + 1] - points[i]);
    }
    return s;
}

// The part of a polyline between arc-length positions a <= b.
std::vector<Vec2> sub_polyline(const std::vector<Vec2>& points, double a, double b) {
    std::vector<Vec2> part;
    double s = 0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const double segment = length(points[i + 1] - points[i]);
        const double start = s;
        const double end = s + segment;
        s = end;
        if (end < a || start > b || segment == 0) {
            continue;
        }
        const auto at = [&](double u) { return points[i] + ((points[i + 1] - points[i]) * ((u - start) / segment)); };
        const Vec2 from = at(std::max(a, start));
        const Vec2 to = at(std::min(b, end));
        if (part.empty() || part.back() != from) {
            part.push_back(from);
        }
        if (part.back() != to) {
            part.push_back(to);
        }
    }
    if (part.empty()) {
        part.push_back(points.front());
    }
    return part;
}

// Where the line through p in direction d meets the polyline, as the
// parameter t of p + t d (|d| = 1) with the smallest |t|, and the segment.
struct Crossing {
    double t = 0;
    Vec2 a;
    Vec2 b;
};

std::optional<Crossing> meet(Vec2 p, Vec2 d, const std::vector<Vec2>& points) {
    std::optional<Crossing> best;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec2 a = points[i];
        const Vec2 e = points[i + 1] - a;
        const double denominator = cross(d, e);
        if (denominator == 0) {
            continue;
        }
        const double t = cross(a - p, e) / denominator;
        const double u = cross(a - p, d) / denominator;
        const double across = std::abs(t * denominator) / length(e); // |t| sin(angle)
        if (u < 0 || u > 1 || across > max_across || std::abs(t) > max_end_move) {
            continue;
        }
        if (!best || std::abs(t) < std::abs(best->t)) {
            best = Crossing{.t = t, .a = a, .b = points[i + 1]};
        }
    }
    return best;
}

// Whether the line crosses the contour near its end, or its end lies on
// it: the end pixel or one of the two before it is a contour pixel, or the
// end and the pixel two before it lie on different sides of the contour's
// line. A line that crosses a contour and ends just past it does not end
// on it (the player saw it continue).
bool crosses_near_end(const Line& line, std::size_t end, const Line& contour) {
    const std::size_t n = line.path.size();
    const auto back = [&](std::size_t k) {
        return end == line.first() ? line.path[std::min(k, n - 1)] : line.path[n - 1 - std::min(k, n - 1)];
    };
    for (std::size_t k = 0; k <= 2; ++k) {
        if (std::ranges::find(contour.path, back(k)) != contour.path.end()) {
            return true;
        }
    }
    const Vec2 a = centre_of(contour.first());
    const Vec2 b = centre_of(contour.last());
    return cross(b - a, centre_of(end) - a) * cross(b - a, centre_of(back(2)) - a) < 0;
}

class Pass {
public:
    Pass(std::vector<Line> lines, std::span<const std::int32_t> owners, SceneStats& stats)
        : lines_(std::move(lines)), owners_(owners), stats_(&stats) {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            for (const std::size_t pixel : lines_[i].path) {
                drawn_.emplace_back(pixel, i);
            }
        }
        std::ranges::stable_sort(drawn_);
    }

    void run() {
        clip_ends();
        close_joints();
        hide_runs();
    }

    std::vector<Line> take() { return std::move(lines_); }

private:
    [[nodiscard]] bool seen(std::size_t pixel, std::size_t line) const {
        return owners_[pixel] == lines_[line].primitive;
    }

    // The lines that drew the pixel.
    template <typename Visit>
    void lines_at(std::size_t pixel, Visit visit) const {
        auto it = std::ranges::lower_bound(drawn_, std::pair{pixel, std::size_t{0}});
        for (; it != drawn_.end() && it->first == pixel; ++it) {
            visit(it->second);
        }
    }

    template <typename Visit>
    void neighbours(std::size_t pixel, Visit visit) const {
        const int x = pixel_x(pixel);
        const int y = pixel_y(pixel);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if ((dx != 0 || dy != 0) && nx >= 0 && nx < picture_width && ny >= 0 && ny < picture_height) {
                    visit((static_cast<std::size_t>(ny) * picture_width) + static_cast<std::size_t>(nx));
                }
            }
        }
    }

    // Whether the line's end pixel is at its stroke's first point (path 1
    // may draw a line from either end).
    static bool at_front(const Line& line, std::size_t end) {
        const auto& points = line.strokes.front().points;
        const Vec2 c = centre_of(end);
        return length(points.front() - c) <= length(points.back() - c);
    }

    // The stroke point next to the line's end point (the end itself for a
    // dot).
    static Vec2 next_point(const Line& line, std::size_t end) {
        const auto& points = line.strokes.front().points;
        if (points.size() < 2) {
            return points.front();
        }
        return at_front(line, end) ? points[1] : points[points.size() - 2];
    }

    // The stroke point at the line's end pixel.
    static Vec2& end_point(Line& line, std::size_t end) {
        Stroke& stroke = line.strokes.front();
        return at_front(line, end) ? stroke.points.front() : stroke.points.back();
    }

    void record(IntegrityChangeKind kind, std::size_t line, std::size_t other, std::size_t pixel,
                const char* evidence) {
        stats_->integrity_changes.push_back(IntegrityChange{
            .kind = kind,
            .primitive = lines_[line].primitive,
            .other = lines_[other].primitive,
            .x = pixel_x(pixel),
            .y = pixel_y(pixel),
            .evidence = evidence,
        });
    }

    // Line ends on touching pixels, both seen, are joined by a short
    // connector from one end point to the other, drawn as part of the line
    // drawn later. No end moves, so one joint never undoes another.
    void close_joints() {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            for (const std::size_t end : {lines_[i].first(), lines_[i].last()}) {
                if (!seen(end, i)) {
                    continue;
                }
                neighbours(end, [&](std::size_t q) {
                    lines_at(q, [&](std::size_t j) {
                        if (j == i || !seen(q, j)) {
                            return;
                        }
                        if (lines_[j].is_end(q)) {
                            if (j > i) {
                                close_joint(i, end, j, q);
                            }
                        } else if (touching(lines_[j].first(), end) || touching(lines_[j].last(), end) ||
                                   lines_[j].is_end(end)) {
                            join_near_end(i, end, j);
                        }
                    });
                });
            }
        }
    }

    // A short stroke of the line from a to b, with the line's cuts (it ends
    // on the same contours) and an extra cut if given.
    static void add_connector(Line& line, Vec2 a, Vec2 b, std::optional<Cut> cut) {
        Stroke connector = line.strokes.front();
        connector.points = {a, b};
        connector.fitted = false;
        if (cut) {
            connector.cuts.push_back(*cut);
        }
        line.connectors.push_back(std::move(connector));
    }

    // The nearest point of the line's own strokes to p, and the segment it
    // lies on.
    [[nodiscard]] std::optional<std::tuple<Vec2, Vec2, Vec2>> nearest_on(std::size_t line, Vec2 p) const {
        std::optional<std::tuple<Vec2, Vec2, Vec2>> best;
        double best_distance = 1e300;
        for (const Stroke& stroke : lines_[line].strokes) {
            for (std::size_t k = 0; k + 1 < stroke.points.size(); ++k) {
                const Vec2 a = stroke.points[k];
                const Vec2 d = stroke.points[k + 1] - a;
                const double length_squared = dot(d, d);
                if (length_squared == 0) {
                    continue;
                }
                const double t = std::clamp(dot(p - a, d) / length_squared, 0.0, 1.0);
                const Vec2 x = a + (d * t);
                if (length(p - x) < best_distance) {
                    best_distance = length(p - x);
                    best = std::tuple{x, a, stroke.points[k + 1]};
                }
            }
        }
        return best;
    }

    void close_joint(std::size_t i, std::size_t end, std::size_t j, std::size_t q) {
        const Vec2 a = end_point(lines_[i], end);
        const Vec2 b = end_point(lines_[j], q);
        if (length(a - b) <= joined) {
            return;
        }
        // The connector belongs to the line drawn first, so the later line
        // and everything after it keep showing over it.
        const bool i_later = lines_[i].primitive < lines_[j].primitive;
        Line& owner = lines_[i_later ? i : j];
        add_connector(owner, i_later ? a : b, i_later ? b : a, std::nullopt);
        ++stats_->closed_joints;
        record(IntegrityChangeKind::ClosedJoint, i_later ? i : j, i_later ? j : i, end, joint_evidence);
    }

    // A line end next to another line close to that line's end (a corner
    // that is not end to end): a connector to the other line's nearest
    // point.
    void join_near_end(std::size_t i, std::size_t end, std::size_t j) {
        const Vec2 p = end_point(lines_[i], end);
        const auto nearest = nearest_on(j, p);
        if (!nearest || length(p - std::get<0>(*nearest)) <= joined) {
            return;
        }
        // Cut along the other line's near edge, so it never covers that line.
        const Vec2 x = std::get<0>(*nearest);
        const Vec2 u = (p - x) * (1 / length(p - x));
        const Cut cut{.point = x + (u * lines_[j].strokes.front().radius), .normal = u * -1};
        add_connector(lines_[i], p, x, cut);
        ++stats_->closed_joints;
        record(IntegrityChangeKind::ClosedJoint, i, j, end, near_end_evidence);
    }

    // The contours a line end touches: lines passing next to the end pixel,
    // seen there, without an end of their own near it, that the ending line
    // does not cross near its end.
    [[nodiscard]] std::vector<std::size_t> contours_at(std::size_t line, std::size_t end) const {
        std::vector<std::size_t> found;
        neighbours(end, [&](std::size_t q) {
            lines_at(q, [&](std::size_t j) {
                if (j == line || !seen(q, j)) {
                    return;
                }
                const Line& other = lines_[j];
                const bool end_near = other.first() == end || other.last() == end || touching(other.first(), end) ||
                                      touching(other.last(), end);
                if (!end_near && !crosses_near_end(lines_[line], end, other)) {
                    found.push_back(j);
                }
            });
        });
        std::ranges::stable_sort(found);
        const auto repeated = std::ranges::unique(found);
        found.erase(repeated.begin(), repeated.end());
        return found;
    }

    // Line ends on another line's contour: extended or trimmed to its
    // centreline and cut along its near edge.
    void clip_ends() {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            Line& line = lines_[i];
            if (line.path.size() < 2 || line.strokes.front().points.size() < 2) {
                continue;
            }
            for (const std::size_t end : {line.first(), line.last()}) {
                if (seen(end, i)) {
                    clip_end(i, end);
                }
            }
        }
    }

    void clip_end(std::size_t i, std::size_t end) {
        const std::vector<std::size_t> contours = contours_at(i, end);
        if (contours.empty()) {
            return;
        }
        Stroke& stroke = lines_[i].strokes.front();
        auto& points = stroke.points;
        const bool front = at_front(lines_[i], end);
        const Vec2 p = front ? points.front() : points.back();
        const Vec2 q = front ? points[1] : points[points.size() - 2];
        const double segment = length(p - q);
        if (segment == 0) {
            return;
        }
        const Vec2 d = (p - q) * (1 / segment);
        std::optional<Crossing> best;
        std::size_t best_line = contours.front();
        for (const std::size_t j : contours) {
            for (const Stroke& other : lines_[j].strokes) {
                const auto crossing = meet(p, d, other.points);
                if (crossing && crossing->t > -segment && (!best || std::abs(crossing->t) < std::abs(best->t))) {
                    best = crossing;
                    best_line = j;
                }
            }
        }
        if (!best) {
            join_side(i, end, contours);
            return;
        }
        const Vec2 x = p + (d * best->t);
        const Vec2 s = best->b - best->a;
        Vec2 u{.x = -s.y / length(s), .y = s.x / length(s)};
        if (dot(u, q - x) < 0) {
            u = u * -1;
        }
        const double radius = lines_[best_line].strokes.front().radius;
        const Cut cut{.point = x + (u * radius), .normal = u * -1};
        if (!cut.keeps(centre_of(end))) {
            ++stats_->kept_contacts;
            record(IntegrityChangeKind::KeptContact, i, best_line, end, on_contour_evidence);
            return;
        }
        // Every other point of the line must lie on its own side.
        for (std::size_t k = 0; k < points.size(); ++k) {
            const bool is_end = front ? k == 0 : k + 1 == points.size();
            if (!is_end && !cut.keeps(points[k])) {
                ++stats_->kept_contacts;
                record(IntegrityChangeKind::KeptContact, i, best_line, end, passes_evidence);
                return;
            }
        }
        (front ? points.front() : points.back()) = x;
        stroke.cuts.push_back(cut);
        ++stats_->clipped_ends;
        record(IntegrityChangeKind::ClippedEnd, i, best_line, end, contact_evidence);
    }

    // A line end beside a contour, pointing along or away from it: a
    // connector from the end to the contour's nearest point, cut along the
    // contour's near edge.
    void join_side(std::size_t i, std::size_t end, const std::vector<std::size_t>& contours) {
        const Vec2 p = end_point(lines_[i], end);
        std::optional<std::tuple<Vec2, Vec2, Vec2>> best;
        std::size_t best_line = contours.front();
        for (const std::size_t j : contours) {
            const auto nearest = nearest_on(j, p);
            if (nearest && (!best || length(p - std::get<0>(*nearest)) < length(p - std::get<0>(*best)))) {
                best = nearest;
                best_line = j;
            }
        }
        const double distance = best ? length(p - std::get<0>(*best)) : 1e300;
        if (!best || distance > max_across || distance == 0) {
            ++stats_->kept_contacts;
            record(IntegrityChangeKind::KeptContact, i, contours.front(), end, no_crossing_evidence);
            return;
        }
        const Vec2 x = std::get<0>(*best);
        const Vec2 u = (p - x) * (1 / distance);
        const double radius = lines_[best_line].strokes.front().radius;
        const Cut cut{.point = x + (u * radius), .normal = u * -1};
        if (!cut.keeps(centre_of(end))) {
            ++stats_->kept_contacts;
            record(IntegrityChangeKind::KeptContact, i, best_line, end, on_contour_evidence);
            return;
        }
        add_connector(lines_[i], p, x, cut);
        // The line itself is cut too where nothing but its end lies past the
        // edge.
        Stroke& stroke = lines_[i].strokes.front();
        const bool front = at_front(lines_[i], end);
        bool only_end = true;
        for (std::size_t k = 0; k < stroke.points.size(); ++k) {
            const bool is_end = front ? k == 0 : k + 1 == stroke.points.size();
            only_end = only_end && (is_end || cut.keeps(stroke.points[k]));
        }
        if (only_end) {
            stroke.cuts.push_back(cut);
        }
        ++stats_->clipped_ends;
        record(IntegrityChangeKind::ClippedEnd, i, best_line, end, side_evidence);
    }

    // A line's stroke is removed along the pixels it drew that show
    // another object at 1x: half a pixel to each side of their centres,
    // so the stroke's ends still reach into the object over it. A boundary
    // drawn by two lines thus shows once, in the colour seen at 1x, and no
    // sliver of a covered line shows between the strokes over it.
    void hide_runs() {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            std::vector<std::size_t> hidden;
            std::ranges::copy_if(lines_[i].path, std::back_inserter(hidden),
                                 [&](std::size_t p) { return !seen(p, i); });
            if (!hidden.empty()) {
                hide(i, hidden);
            }
        }
    }

    void hide(std::size_t i, const std::vector<std::size_t>& hidden) {
        std::vector<Stroke> pieces;
        bool changed = false;
        for (Stroke& stroke : lines_[i].strokes) {
            if (stroke.points.size() < 2) {
                pieces.push_back(std::move(stroke));
                continue;
            }
            // The hidden pixels near this stroke, as positions along it.
            std::vector<double> along;
            for (const std::size_t p : hidden) {
                const auto [s, distance] = closest(stroke.points, centre_of(p));
                if (distance <= 1.0) {
                    along.push_back(s);
                }
            }
            if (along.empty()) {
                pieces.push_back(std::move(stroke));
                continue;
            }
            std::ranges::stable_sort(along);
            const double end = total_length(stroke.points);
            double kept_from = 0;
            for (std::size_t k = 0; k < along.size();) {
                std::size_t n = k + 1;
                while (n < along.size() && along[n] - along[n - 1] <= run_gap) {
                    ++n;
                }
                const double cut_from = std::max(along[k] - 0.5, 0.0);
                if (cut_from > kept_from) {
                    Stroke piece = stroke;
                    piece.points = sub_polyline(stroke.points, kept_from, cut_from);
                    pieces.push_back(std::move(piece));
                }
                kept_from = std::min(along[n - 1] + 0.5, end);
                k = n;
            }
            if (kept_from < end) {
                Stroke piece = stroke;
                piece.points = sub_polyline(stroke.points, kept_from, end);
                pieces.push_back(std::move(piece));
            }
            changed = true;
        }
        lines_[i].strokes = std::move(pieces);
        if (changed) {
            const std::int32_t over = owners_[hidden.front()];
            ++stats_->merged_runs;
            stats_->integrity_changes.push_back(IntegrityChange{
                .kind = IntegrityChangeKind::MergedRun,
                .primitive = lines_[i].primitive,
                .other = over,
                .x = pixel_x(hidden.front()),
                .y = pixel_y(hidden.front()),
                .evidence = hidden_evidence,
            });
        }
    }

    std::vector<Line> lines_;
    std::span<const std::int32_t> owners_;
    SceneStats* stats_;
    std::vector<std::pair<std::size_t, std::size_t>> drawn_; // (pixel, line), sorted.
};

} // namespace

void apply_stroke_integrity(std::vector<Layer>& layers, std::span<const StrokeLine> lines,
                            std::span<const std::int32_t> owners, SceneStats& stats) {
    // The lines with exactly one stroke and a path.
    std::map<std::int32_t, std::size_t> layer_of;
    std::map<std::int32_t, int> strokes_of;
    for (std::size_t k = 0; k < layers.size(); ++k) {
        if (const auto* stroke = std::get_if<Stroke>(&layers[k]); stroke != nullptr && stroke->source >= 0) {
            layer_of[stroke->source] = k;
            ++strokes_of[stroke->source];
        }
    }
    std::vector<Line> pass_lines;
    for (const StrokeLine& line : lines) {
        const auto it = layer_of.find(line.primitive);
        if (it == layer_of.end() || strokes_of[line.primitive] != 1 || line.path.empty()) {
            continue;
        }
        pass_lines.push_back(Line{
            .primitive = line.primitive,
            .path = line.path,
            .strokes = {std::get<Stroke>(layers[it->second])},
            .connectors = {},
        });
    }
    Pass pass(std::move(pass_lines), owners, stats);
    pass.run();
    std::map<std::int32_t, std::vector<Stroke>> result;
    for (Line& line : pass.take()) {
        result[line.primitive] = std::move(line.strokes);
        auto& strokes = result[line.primitive];
        strokes.insert(strokes.end(), line.connectors.begin(), line.connectors.end());
    }
    std::vector<Layer> rebuilt;
    rebuilt.reserve(layers.size());
    for (Layer& layer : layers) {
        const auto* stroke = std::get_if<Stroke>(&layer);
        const auto it = stroke != nullptr ? result.find(stroke->source) : result.end();
        if (it == result.end()) {
            rebuilt.push_back(std::move(layer));
            continue;
        }
        for (Stroke& piece : it->second) {
            rebuilt.emplace_back(std::move(piece));
        }
    }
    layers = std::move(rebuilt);
}

} // namespace sci::vector::detail
