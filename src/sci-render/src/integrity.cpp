// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/render/integrity.hpp"

#include "sci/core/assert.hpp"
#include "sci/pic/geometry.hpp"
#include "sci/render/colors.hpp"
#include "sci/render/rasterize.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <map>
#include <span>
#include <tuple>
#include <utility>
#include <variant>

namespace sci::render {

namespace {

using vector::picture_height;
using vector::picture_pixels;
using vector::picture_width;
using vector::Vec2;

constexpr std::int32_t no_primitive = -1;

// Overshoot is counted from this many samples on: a single sample can lie
// beyond a centreline through rounding at the end of a stroke.
constexpr int min_overshoot_samples = 2;

constexpr std::array<std::pair<int, int>, 4> four_neighbours{
    std::pair{1, 0},
    std::pair{-1, 0},
    std::pair{0, 1},
    std::pair{0, -1},
};

std::size_t pixel_index(int x, int y) {
    return (static_cast<std::size_t>(y) * picture_width) + static_cast<std::size_t>(x);
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

bool in_picture(int x, int y) {
    return x >= 0 && x < picture_width && y >= 0 && y < picture_height;
}

// Whether two pixels are the same or touch, among the eight neighbours.
bool near_pixel(std::size_t a, std::size_t b) {
    return std::abs(pixel_x(a) - pixel_x(b)) <= 1 && std::abs(pixel_y(a) - pixel_y(b)) <= 1;
}

// The picture pixels around a pixel, without it.
template <typename Visit>
void for_each_neighbour(std::size_t pixel, Visit visit) {
    const int x = pixel_x(pixel);
    const int y = pixel_y(pixel);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx != 0 || dy != 0) && in_picture(x + dx, y + dy)) {
                visit(pixel_index(x + dx, y + dy));
            }
        }
    }
}

std::int32_t source_of(const vector::Layer& layer) {
    return std::visit([](const auto& l) { return l.source; }, layer);
}

// The distance from p to the segment a-b, and the side of the segment's
// line that p lies on (the sign of the cross product).
std::pair<double, double> segment_distance(Vec2 p, Vec2 a, Vec2 b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length_squared = (dx * dx) + (dy * dy);
    const double t =
        length_squared == 0 ? 0 : std::clamp((((p.x - a.x) * dx) + ((p.y - a.y) * dy)) / length_squared, 0.0, 1.0);
    const double ex = p.x - a.x - (t * dx);
    const double ey = p.y - a.y - (t * dy);
    return {std::sqrt((ex * ex) + (ey * ey)), (dx * (p.y - a.y)) - (dy * (p.x - a.x))};
}

// What the checks know about a visible line.
struct LineInfo {
    bool is_line = false;
    std::vector<std::size_t> path; // The pixels it drew, in order (picture pixels).
    std::vector<const vector::Stroke*> strokes;

    [[nodiscard]] std::size_t first() const { return path.front(); }

    [[nodiscard]] std::size_t last() const { return path.back(); }

    // Whether one of the line's strokes covers p (its cuts included).
    [[nodiscard]] bool covers(Vec2 p) const {
        for (const vector::Stroke* stroke : strokes) {
            const auto& points = stroke->points;
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (segment_distance(p, points[i], points[std::min(i + 1, points.size() - 1)]).first <=
                        stroke->radius &&
                    stroke->kept(p)) {
                    return true;
                }
            }
        }
        return false;
    }

    // The distance from p to the nearest stroke, and p's side of that
    // stroke's nearest segment.
    [[nodiscard]] std::pair<double, double> nearest(Vec2 p) const {
        std::pair<double, double> best{1e300, 0};
        for (const vector::Stroke* stroke : strokes) {
            const auto& points = stroke->points;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const auto found = segment_distance(p, points[i], points[std::min(i + 1, points.size() - 1)]);
                if (found.first < best.first) {
                    best = found;
                }
            }
        }
        return best;
    }
};

// The 1x side: owners, priorities and the edge band of the picture area.
struct OneX {
    std::vector<std::int32_t> owner = std::vector<std::int32_t>(picture_pixels, no_primitive);
    std::vector<std::uint8_t> priority = std::vector<std::uint8_t>(picture_pixels, 0);
    std::vector<bool> band = std::vector<bool>(picture_pixels, false);
};

// Pixels with a neighbour of another pair class, among their eight.
std::vector<bool> edge_band(const std::vector<std::uint32_t>& classes) {
    std::vector<bool> band(picture_pixels, false);
    for (std::size_t i = 0; i < picture_pixels; ++i) {
        for_each_neighbour(i, [&](std::size_t q) {
            if (classes[q] != classes[i]) {
                band[i] = true;
            }
        });
    }
    return band;
}

OneX read_1x(const pic::PictureMaps& maps, const pic::DrawTrace& trace) {
    OneX one;
    std::vector<std::uint32_t> classes(picture_pixels);
    for (std::size_t i = 0; i < picture_pixels; ++i) {
        const std::size_t screen = pic::PictureMaps::index(pixel_x(i), pixel_y(i) + pic::picture_top);
        one.owner[i] = trace.visual_owner[screen];
        one.priority[i] = maps.priority[screen];
        const Rgb8 c = pair_color(maps.pairs[screen]);
        classes[i] = (std::uint32_t{c.r} << 16U) | (std::uint32_t{c.g} << 8U) | c.b;
    }
    one.band = edge_band(classes);
    return one;
}

std::vector<LineInfo> read_lines(const vector::Scene& scene, const pic::DrawTrace& trace) {
    std::vector<LineInfo> lines(trace.primitives.size());
    for (const vector::Layer& layer : scene.layers) {
        const auto* stroke = std::get_if<vector::Stroke>(&layer);
        if (stroke != nullptr && stroke->source >= 0 &&
            std::holds_alternative<pic::Line>(trace.primitives[static_cast<std::size_t>(stroke->source)].shape)) {
            lines[static_cast<std::size_t>(stroke->source)].strokes.push_back(stroke);
        }
    }
    for (std::size_t p = 0; p < lines.size(); ++p) {
        LineInfo& info = lines[p];
        if (info.strokes.empty()) {
            continue;
        }
        const auto& line = std::get<pic::Line>(trace.primitives[p].shape);
        const auto screen = [](pic::Point q) { return pic::ScreenPoint{.x = q.x, .y = q.y + pic::picture_top}; };
        pic::for_each_line_pixel(screen(line.from), screen(line.to), [&](int x, int y) {
            if (in_picture(x, y - pic::picture_top)) {
                info.path.push_back(pixel_index(x, y - pic::picture_top));
            }
        });
        info.is_line = !info.path.empty();
    }
    return lines;
}

// Every sample: the primitive whose layer shows there, or no_primitive
// where a face shows.
class Grid {
public:
    Grid(const vector::Scene& scene, int density, core::Executor& executor)
        : density_(density), width_(picture_width * density), height_(picture_height * density),
          source_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), no_primitive) {
        const SceneSampler sampler(scene);
        std::vector<std::int32_t> layer_source(scene.layers.size());
        std::ranges::transform(scene.layers, layer_source.begin(), source_of);
        executor.run(static_cast<std::size_t>(height_), [&](std::size_t row) {
            const std::span<std::int32_t> out =
                std::span(source_).subspan(row * static_cast<std::size_t>(width_), static_cast<std::size_t>(width_));
            sampler.sample_sources((static_cast<double>(row) + 0.5) / density_, density_, out);
            for (std::int32_t& s : out) {
                s = s >= 0 ? layer_source[static_cast<std::size_t>(s)] : no_primitive;
            }
        });
    }

    [[nodiscard]] int width() const { return width_; }

    [[nodiscard]] int height() const { return height_; }

    [[nodiscard]] int density() const { return density_; }

    [[nodiscard]] std::size_t index(int sx, int sy) const {
        return (static_cast<std::size_t>(sy) * static_cast<std::size_t>(width_)) + static_cast<std::size_t>(sx);
    }

    [[nodiscard]] std::int32_t at(int sx, int sy) const { return source_[index(sx, sy)]; }

    [[nodiscard]] bool inside(int sx, int sy) const { return sx >= 0 && sx < width_ && sy >= 0 && sy < height_; }

    [[nodiscard]] std::size_t pixel_of(int sx, int sy) const { return pixel_index(sx / density_, sy / density_); }

    [[nodiscard]] Vec2 point(int sx, int sy) const { return {.x = (sx + 0.5) / density_, .y = (sy + 0.5) / density_}; }

private:
    int density_;
    int width_;
    int height_;
    std::vector<std::int32_t> source_;
};

// A window of pixels, clipped to the picture, as a range of samples.
struct Window {
    int left = 0;
    int top = 0;
    int right = 0; // Exclusive.
    int bottom = 0;

    static Window around(std::size_t a, std::size_t b, int margin, int density) {
        return {
            .left = std::max(std::min(pixel_x(a), pixel_x(b)) - margin, 0) * density,
            .top = std::max(std::min(pixel_y(a), pixel_y(b)) - margin, 0) * density,
            .right = std::min(std::max(pixel_x(a), pixel_x(b)) + margin + 1, picture_width) * density,
            .bottom = std::min(std::max(pixel_y(a), pixel_y(b)) + margin + 1, picture_height) * density,
        };
    }

    [[nodiscard]] bool contains(int sx, int sy) const { return sx >= left && sx < right && sy >= top && sy < bottom; }
};

// Whether a sample showing the line, inside the window, lies in or next to
// (one sample away from) the other line's stroke shape, whatever is drawn
// over that shape: a gap between the two strokes, not what covers them,
// breaks a joint.
bool meet_within(const Grid& grid, const Window& w, std::int32_t line, const LineInfo& other) {
    const double step = 1.0 / grid.density();
    for (int sy = w.top; sy < w.bottom; ++sy) {
        for (int sx = w.left; sx < w.right; ++sx) {
            if (grid.at(sx, sy) != line) {
                continue;
            }
            const Vec2 p = grid.point(sx, sy);
            for (const auto [dx, dy] :
                 {std::pair{0, 0}, std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                if (other.covers({.x = p.x + (dx * step), .y = p.y + (dy * step)})) {
                    return true;
                }
            }
        }
    }
    return false;
}

// The smallest gap between samples showing the line in the window and the
// other line's strokes (cuts ignored); the window's size if none shows.
double joint_gap(const Grid& grid, const Window& w, std::int32_t line, const LineInfo& other) {
    double gap = static_cast<double>(w.right - w.left) / grid.density();
    for (int sy = w.top; sy < w.bottom; ++sy) {
        for (int sx = w.left; sx < w.right; ++sx) {
            if (grid.at(sx, sy) == line) {
                gap = std::min(gap, other.nearest(grid.point(sx, sy)).first - other.strokes.front()->radius);
            }
        }
    }
    return std::max(gap, 0.0);
}

void add_case(Integrity& result, IntegrityKind kind, std::pair<std::int32_t, std::int32_t> primitives,
              std::size_t pixel, double size) {
    result.cases.push_back(IntegrityCase{
        .kind = kind,
        .primitive = primitives.first,
        .other = primitives.second,
        .x = pixel_x(pixel),
        .y = pixel_y(pixel),
        .size = size,
    });
}

// The 8-connected groups of pixels each line owns at 1x.
struct Groups {
    std::vector<std::int32_t> of_pixel = std::vector<std::int32_t>(picture_pixels, -1);
    std::vector<std::int32_t> line;
    std::vector<std::size_t> pixel; // One pixel of each group.
};

Groups owned_groups(const OneX& one, const std::vector<LineInfo>& lines) {
    Groups groups;
    for (std::size_t start = 0; start < picture_pixels; ++start) {
        const std::int32_t owner = one.owner[start];
        if (owner < 0 || !lines[static_cast<std::size_t>(owner)].is_line || groups.of_pixel[start] >= 0) {
            continue;
        }
        const auto id = static_cast<std::int32_t>(groups.line.size());
        groups.line.push_back(owner);
        groups.pixel.push_back(start);
        groups.of_pixel[start] = id;
        std::vector<std::size_t> pending{start};
        while (!pending.empty()) {
            const std::size_t i = pending.back();
            pending.pop_back();
            for_each_neighbour(i, [&](std::size_t q) {
                if (one.owner[q] == owner && groups.of_pixel[q] < 0) {
                    groups.of_pixel[q] = id;
                    pending.push_back(q);
                }
            });
        }
    }
    return groups;
}

// The owned groups that one 4-connected group of samples showing a line
// touches; the samples are marked seen.
std::vector<std::int32_t> component_groups(const Grid& grid, const OneX& one, const Groups& groups, int sx, int sy,
                                           std::vector<bool>& seen, int& samples) {
    const std::int32_t line = grid.at(sx, sy);
    std::vector<std::int32_t> touched;
    samples = 0;
    std::vector<std::pair<int, int>> pending{{sx, sy}};
    seen[grid.index(sx, sy)] = true;
    while (!pending.empty()) {
        const auto [x, y] = pending.back();
        pending.pop_back();
        ++samples;
        const std::size_t pixel = grid.pixel_of(x, y);
        if (one.owner[pixel] == line) {
            touched.push_back(groups.of_pixel[pixel]);
        }
        for (const auto [dx, dy] : four_neighbours) {
            if (grid.inside(x + dx, y + dy) && grid.at(x + dx, y + dy) == line && !seen[grid.index(x + dx, y + dy)]) {
                seen[grid.index(x + dx, y + dy)] = true;
                pending.emplace_back(x + dx, y + dy);
            }
        }
    }
    std::ranges::stable_sort(touched);
    const auto repeated = std::ranges::unique(touched);
    touched.erase(repeated.begin(), repeated.end());
    return touched;
}

// Fragments and breaks within a line: the 4-connected sample groups of
// every line against the 8-connected groups of the pixels it owns.
void check_groups(const Grid& grid, const OneX& one, const std::vector<LineInfo>& lines, Integrity& result) {
    const Groups groups = owned_groups(one, lines);
    result.line_groups = static_cast<int>(groups.line.size());
    std::vector<int> components(groups.line.size(), 0);
    std::vector<bool> seen(static_cast<std::size_t>(grid.width()) * static_cast<std::size_t>(grid.height()), false);
    for (int sy = 0; sy < grid.height(); ++sy) {
        for (int sx = 0; sx < grid.width(); ++sx) {
            const std::int32_t line = grid.at(sx, sy);
            if (line < 0 || seen[grid.index(sx, sy)] || !lines[static_cast<std::size_t>(line)].is_line) {
                continue;
            }
            int samples = 0;
            const std::vector<std::int32_t> touched = component_groups(grid, one, groups, sx, sy, seen, samples);
            if (touched.empty()) {
                ++result.fragments;
                add_case(result, IntegrityKind::Fragment, {line, no_primitive}, grid.pixel_of(sx, sy),
                         samples / static_cast<double>(grid.density() * grid.density()));
            }
            for (const std::int32_t g : touched) {
                ++components[static_cast<std::size_t>(g)];
            }
        }
    }
    // A group shown in pieces, or not at all, is broken.
    for (std::size_t g = 0; g < groups.line.size(); ++g) {
        if (components[g] == 1) {
            continue;
        }
        ++(components[g] == 0 ? result.hidden_groups : result.split_groups);
        ++result.breaks;
        add_case(result, IntegrityKind::Break, {groups.line[g], no_primitive}, groups.pixel[g], components[g]);
    }
}

// Joints: the end pixel of a line, seen at 1x, next to a pixel another line
// owns. One per line end and other line.
std::vector<std::tuple<std::int32_t, std::int32_t, std::size_t, std::size_t>>
find_joints(const OneX& one, const std::vector<LineInfo>& lines) {
    std::vector<std::tuple<std::int32_t, std::int32_t, std::size_t, std::size_t>> joints;
    for (std::size_t l = 0; l < lines.size(); ++l) {
        if (!lines[l].is_line) {
            continue;
        }
        for (const std::size_t end : {lines[l].first(), lines[l].last()}) {
            if (std::cmp_not_equal(one.owner[end], l)) {
                continue;
            }
            for_each_neighbour(end, [&](std::size_t q) {
                const std::int32_t other = one.owner[q];
                if (other >= 0 && std::cmp_not_equal(other, l) && lines[static_cast<std::size_t>(other)].is_line) {
                    joints.emplace_back(static_cast<std::int32_t>(l), other, end, q);
                }
            });
        }
    }
    const auto key = [](const auto& j) { return std::tuple{std::get<0>(j), std::get<1>(j), std::get<2>(j)}; };
    std::ranges::stable_sort(joints, {}, key);
    const auto repeated = std::ranges::unique(joints, {}, key);
    joints.erase(repeated.begin(), repeated.end());
    return joints;
}

void check_joints(const Grid& grid, const OneX& one, const std::vector<LineInfo>& lines, Integrity& result) {
    const auto joints = find_joints(one, lines);
    result.joints = static_cast<int>(joints.size());
    std::vector<bool> met(joints.size());
    for (std::size_t j = 0; j < joints.size(); ++j) {
        const auto& [line, other, end, q] = joints[j];
        met[j] =
            meet_within(grid, Window::around(end, q, 1, grid.density()), line, lines[static_cast<std::size_t>(other)]);
    }
    // Joints of one end are adjacent (sorted by line and end).
    const auto same_end = [&](std::size_t a, std::size_t b) {
        return std::get<0>(joints[a]) == std::get<0>(joints[b]) && std::get<2>(joints[a]) == std::get<2>(joints[b]);
    };
    for (std::size_t j = 0; j < joints.size(); ++j) {
        if (met[j]) {
            continue;
        }
        const auto& [line, other, end, q] = joints[j];
        bool met_other = false;
        for (std::size_t k = j; k > 0 && same_end(k - 1, j); --k) {
            met_other = met_other || met[k - 1];
        }
        for (std::size_t k = j + 1; k < joints.size() && same_end(k, j); ++k) {
            met_other = met_other || met[k];
        }
        ++result.broken_joints;
        ++result.breaks;
        result.broken_joints_meeting_another += met_other ? 1 : 0;
        add_case(
            result, IntegrityKind::Break, {line, other}, end,
            joint_gap(grid, Window::around(end, q, 1, grid.density()), line, lines[static_cast<std::size_t>(other)]));
        result.cases.back().meets_another = met_other;
    }
}

// The lines that drew each pixel, sorted by pixel.
std::vector<std::pair<std::size_t, std::int32_t>> drawn_pixels(const std::vector<LineInfo>& lines) {
    std::vector<std::pair<std::size_t, std::int32_t>> drawn;
    for (std::size_t l = 0; l < lines.size(); ++l) {
        if (lines[l].is_line) {
            for (const std::size_t pixel : lines[l].path) {
                drawn.emplace_back(pixel, static_cast<std::int32_t>(l));
            }
        }
    }
    std::ranges::stable_sort(drawn);
    return drawn;
}

// Whether the line crosses the contour near its end, or its end lies on
// it: the end pixel or one of the two before it is a contour pixel, or the
// end and the pixel two before it lie on different sides of the contour's
// line. A line that crosses a contour and ends just past it does not end
// on it.
bool crosses_near_end(const LineInfo& line, std::size_t end, const LineInfo& contour) {
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
    const auto side = [&](Vec2 p) { return ((b.x - a.x) * (p.y - a.y)) - ((b.y - a.y) * (p.x - a.x)); };
    return side(centre_of(end)) * side(centre_of(back(2))) < 0;
}

// The contours a line end touches (the definition the vector path uses):
// lines passing next to the end pixel and seen there, without an end next
// to it, that the ending line does not cross near its end.
std::vector<std::int32_t> contours_at(std::size_t line, std::size_t end, const OneX& one,
                                      const std::vector<LineInfo>& lines,
                                      const std::vector<std::pair<std::size_t, std::int32_t>>& drawn) {
    std::vector<std::int32_t> found;
    for_each_neighbour(end, [&](std::size_t q) {
        auto it = std::ranges::lower_bound(drawn, std::pair{q, std::int32_t{0}});
        for (; it != drawn.end() && it->first == q; ++it) {
            const std::int32_t m = it->second;
            const LineInfo& other = lines[static_cast<std::size_t>(m)];
            if (std::cmp_equal(m, line) || one.owner[q] != m || near_pixel(other.first(), end) ||
                near_pixel(other.last(), end) || crosses_near_end(lines[line], end, other)) {
                continue;
            }
            found.push_back(m);
        }
    });
    std::ranges::stable_sort(found);
    const auto repeated = std::ranges::unique(found);
    found.erase(repeated.begin(), repeated.end());
    return found;
}

// Samples near the end showing the line inside the contour's strokes or
// beyond the contour's line (through the centres of its end pixels; a
// stroke's own segments can run either way, connectors included).
// The samples near the end that show the line inside the contour's stroke
// or beyond its line, and whether the end pixel's centre itself lies there
// (1x shows the line past the contour).
std::pair<int, bool> overshooting_samples(const Grid& grid, std::size_t line, std::size_t end, const LineInfo& info,
                                          const LineInfo& contour) {
    const std::size_t inner = end == info.first() ? info.path[1] : info.path[info.path.size() - 2];
    const Vec2 a = centre_of(contour.first());
    const Vec2 b = centre_of(contour.last());
    const auto side = [&](Vec2 p) { return ((b.x - a.x) * (p.y - a.y)) - ((b.y - a.y) * (p.x - a.x)); };
    const double inner_side = side(centre_of(inner));
    const double radius = contour.strokes.front()->radius;
    const auto past = [&](Vec2 p) {
        const double s = side(p);
        const bool beyond = inner_side != 0 && s != 0 && (s > 0) != (inner_side > 0);
        return beyond || contour.nearest(p).first <= radius;
    };
    const Window w = Window::around(end, end, 2, grid.density());
    int offending = 0;
    for (int sy = w.top; sy < w.bottom; ++sy) {
        for (int sx = w.left; sx < w.right; ++sx) {
            if (std::cmp_equal(grid.at(sx, sy), line) && past(grid.point(sx, sy))) {
                ++offending;
            }
        }
    }
    return {offending, past(centre_of(end))};
}

// Whether the line meets the contour at a shallow angle (under 30
// degrees, from its last three pixels and the contour's end points): it
// runs along the contour rather than ending on it.
bool runs_along(const LineInfo& info, std::size_t end, const LineInfo& contour) {
    const std::size_t n = info.path.size();
    const std::size_t inner = end == info.first() ? info.path[std::min<std::size_t>(2, n - 1)]
                                                  : info.path[n - 1 - std::min<std::size_t>(2, n - 1)];
    const Vec2 d{.x = centre_of(end).x - centre_of(inner).x, .y = centre_of(end).y - centre_of(inner).y};
    const Vec2 a = centre_of(contour.first());
    const Vec2 b = centre_of(contour.last());
    const Vec2 e{.x = b.x - a.x, .y = b.y - a.y};
    const double lengths = std::sqrt(((d.x * d.x) + (d.y * d.y)) * ((e.x * e.x) + (e.y * e.y)));
    return lengths > 0 && std::abs((d.x * e.y) - (d.y * e.x)) < 0.5 * lengths;
}

// Records an overshoot; `classes` holds its past_at_1x and along_contour.
void add_overshoot(Integrity& result, std::pair<std::int32_t, std::int32_t> primitives, std::size_t end, double size,
                   std::pair<bool, bool> classes) {
    ++result.overshoots;
    result.overshoots_past_at_1x += classes.first ? 1 : 0;
    result.overshoots_along += !classes.first && classes.second ? 1 : 0;
    add_case(result, IntegrityKind::Overshoot, primitives, end, size);
    result.cases.back().past_at_1x = classes.first;
    result.cases.back().along_contour = classes.second;
}

void check_overshoot(const Grid& grid, const OneX& one, const std::vector<LineInfo>& lines, Integrity& result) {
    const auto drawn = drawn_pixels(lines);
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const LineInfo& info = lines[l];
        if (!info.is_line || info.path.size() < 2) {
            continue;
        }
        for (const std::size_t end : {info.first(), info.last()}) {
            if (std::cmp_not_equal(one.owner[end], l)) {
                continue;
            }
            for (const std::int32_t m : contours_at(l, end, one, lines, drawn)) {
                ++result.contacts;
                const LineInfo& contour = lines[static_cast<std::size_t>(m)];
                const auto [offending, past_at_1x] = overshooting_samples(grid, l, end, info, contour);
                if (offending >= min_overshoot_samples) {
                    add_overshoot(result, {static_cast<std::int32_t>(l), m}, end,
                                  offending / static_cast<double>(grid.density() * grid.density()),
                                  {past_at_1x, runs_along(info, end, contour)});
                }
            }
        }
    }
}

// Depth: a layer showing at a pixel that is neither its own at 1x nor in
// the edge band, where the 1x priority is nearer than the layer's.
void check_depth(const Grid& grid, const OneX& one, const pic::DrawTrace& trace, Integrity& result) {
    std::vector<bool> counted(picture_pixels, false);
    for (int sy = 0; sy < grid.height(); ++sy) {
        for (int sx = 0; sx < grid.width(); ++sx) {
            const std::int32_t p = grid.at(sx, sy);
            const std::size_t pixel = grid.pixel_of(sx, sy);
            if (p < 0 || one.owner[pixel] == p || one.band[pixel] || counted[pixel]) {
                continue;
            }
            const pic::DrawState& state = trace.primitives[static_cast<std::size_t>(p)].state;
            // A layer without depth of its own leaves the order to the commands.
            if ((state.planes & pic::plane_priority) != 0 && one.priority[pixel] > state.priority) {
                counted[pixel] = true;
                ++result.depth_violations;
                add_case(result, IntegrityKind::Depth, {p, one.owner[pixel]}, pixel, 1);
            }
        }
    }
}

// How many samples show `line` in the pixels.
int samples_showing(const Grid& grid, std::int32_t line, const std::vector<std::size_t>& pixels) {
    const int d = grid.density();
    int shown = 0;
    for (const std::size_t pixel : pixels) {
        for (int sy = pixel_y(pixel) * d; sy < (pixel_y(pixel) + 1) * d; ++sy) {
            for (int sx = pixel_x(pixel) * d; sx < (pixel_x(pixel) + 1) * d; ++sx) {
                shown += grid.at(sx, sy) == line ? 1 : 0;
            }
        }
    }
    return shown;
}

// Doubled boundaries: lines that drew the same pixels; HD must not show one
// where the player saw the other.
void check_doubled(const Grid& grid, const OneX& one, const std::vector<LineInfo>& lines, Integrity& result) {
    const auto drawn = drawn_pixels(lines);
    std::map<std::pair<std::int32_t, std::int32_t>, std::vector<std::size_t>> shared;
    for (std::size_t i = 0; i < drawn.size();) {
        std::size_t j = i;
        while (j < drawn.size() && drawn[j].first == drawn[i].first) {
            ++j;
        }
        for (std::size_t a = i; a < j; ++a) {
            for (std::size_t b = a + 1; b < j; ++b) {
                if (drawn[a].second != drawn[b].second) {
                    shared[std::minmax(drawn[a].second, drawn[b].second)].push_back(drawn[i].first);
                }
            }
        }
        i = j;
    }
    const int threshold = (grid.density() * grid.density()) / 4;
    for (const auto& [pair, pixels] : shared) {
        if (pixels.size() < 2) {
            continue;
        }
        ++result.shared_pairs;
        for (const auto [shown, seen] : {pair, std::pair{pair.second, pair.first}}) {
            std::vector<std::size_t> others;
            std::ranges::copy_if(pixels, std::back_inserter(others),
                                 [&](std::size_t p) { return one.owner[p] == seen; });
            const int samples = samples_showing(grid, shown, others);
            if (samples >= threshold) {
                ++result.doubled;
                add_case(result, IntegrityKind::Doubled, {shown, seen}, others.front(),
                         samples / static_cast<double>(grid.density() * grid.density()));
                break;
            }
        }
    }
}

} // namespace

Integrity check_integrity(const vector::Scene& visual, const pic::PictureMaps& maps, const pic::DrawTrace& trace,
                          core::Executor& executor, int density) {
    SCI_ASSERT(visual.plane == vector::Plane::Visual);
    SCI_ASSERT(density >= 2);
    const OneX one = read_1x(maps, trace);
    const std::vector<LineInfo> lines = read_lines(visual, trace);
    const Grid grid(visual, density, executor);
    Integrity result;
    check_overshoot(grid, one, lines, result);
    check_depth(grid, one, trace, result);
    check_doubled(grid, one, lines, result);
    check_groups(grid, one, lines, result);
    check_joints(grid, one, lines, result);
    return result;
}

} // namespace sci::render
