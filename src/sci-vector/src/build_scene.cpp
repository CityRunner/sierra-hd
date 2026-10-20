// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vector/build_scene.hpp"

#include "sci/pic/geometry.hpp"
#include "topology.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <span>
#include <tuple>
#include <utility>

namespace sci::vector {

namespace {

using core::IntPoint;
using core::RationalPoint;
using detail::TopoSegment;

constexpr int picture_top = pic::picture_top;

// Gaps up to this far (in doubled coordinates: 2 original pixels) are
// closed when the 1x line pixels touch (spec section 6.3).
constexpr std::int64_t gap_limit = 4;

[[nodiscard]] std::size_t picture_index(int x, int y) {
    return (static_cast<std::size_t>(y) * picture_width) + static_cast<std::size_t>(x);
}

// The plane's values and owners for the picture area.
struct PlaneView {
    std::vector<std::uint8_t> values = std::vector<std::uint8_t>(picture_pixels, 0);
    std::vector<std::int32_t> owners = std::vector<std::int32_t>(picture_pixels, pic::no_owner);
};

PlaneView view_plane(const pic::PictureMaps& maps, const pic::DrawTrace& trace, Plane plane) {
    PlaneView view;
    const std::size_t offset = static_cast<std::size_t>(picture_top) * picture_width;
    const auto& values = plane == Plane::Visual ? maps.pairs : maps.priority;
    const auto& owners = plane == Plane::Visual ? trace.visual_owner : trace.priority_owner;
    std::copy_n(values.begin() + static_cast<std::ptrdiff_t>(offset), picture_pixels, view.values.begin());
    std::copy_n(owners.begin() + static_cast<std::ptrdiff_t>(offset), picture_pixels, view.owners.begin());
    return view;
}

bool in_plane(const pic::TracedPrimitive& primitive, Plane plane) {
    const std::uint8_t bit = plane == Plane::Visual ? pic::plane_visual : pic::plane_priority;
    return (primitive.state.planes & bit) != 0;
}

std::uint8_t value_of(const pic::TracedPrimitive& primitive, Plane plane) {
    return plane == Plane::Visual ? primitive.state.pair : primitive.state.priority;
}

// A line as path 1 drew it: end points clipped to the screen.
struct DrawnLine {
    std::size_t primitive = 0;
    pic::ScreenPoint from;
    pic::ScreenPoint to;

    [[nodiscard]] bool is_dot() const { return from == to; }
};

// The doubled picture coordinates of a pixel centre given in screen
// coordinates.
IntPoint doubled(pic::ScreenPoint p) {
    return {.x = (2 * std::int64_t{p.x}) + 1, .y = (2 * std::int64_t{p.y - picture_top}) + 1};
}

Vec2 centre(pic::ScreenPoint p) {
    return {.x = p.x + 0.5, .y = (p.y - picture_top) + 0.5};
}

std::vector<DrawnLine> collect_lines(const pic::DrawTrace& trace, Plane plane) {
    std::vector<DrawnLine> lines;
    for (std::size_t i = 0; i < trace.primitives.size(); ++i) {
        const auto& primitive = trace.primitives[i];
        const auto* line = std::get_if<pic::Line>(&primitive.shape);
        if (line == nullptr || !in_plane(primitive, plane)) {
            continue;
        }
        const auto to_screen = [](pic::Point p) {
            return pic::clip_to_screen(pic::ScreenPoint{.x = p.x, .y = p.y + picture_top});
        };
        lines.push_back(DrawnLine{.primitive = i, .from = to_screen(line->from), .to = to_screen(line->to)});
    }
    return lines;
}

// Which lines set each screen pixel, as (pixel, line) pairs in order.
std::vector<std::pair<std::int32_t, std::int32_t>> index_line_pixels(const std::vector<DrawnLine>& lines) {
    std::vector<std::pair<std::int32_t, std::int32_t>> pixels;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        pic::for_each_line_pixel(lines[i].from, lines[i].to, [&](int x, int y) {
            pixels.emplace_back((y * pic::screen_width) + x, static_cast<std::int32_t>(i));
        });
    }
    std::ranges::stable_sort(pixels);
    const auto repeated = std::ranges::unique(pixels);
    pixels.erase(repeated.begin(), repeated.end());
    return pixels;
}

// How far from a line's end point its pixels are checked for contact
// with other lines (in original pixels).
constexpr int contact_radius = 2;

bool on_screen(int x, int y) {
    return x >= 0 && x < pic::screen_width && y >= 0 && y < pic::screen_height;
}

// Whether p lies within gap_limit of the integer point e in both axes.
bool near(IntPoint e, const RationalPoint& p) {
    const std::int64_t dx = core::checked_sub(p.x_num(), core::checked_mul(e.x, p.den()));
    const std::int64_t dy = core::checked_sub(p.y_num(), core::checked_mul(e.y, p.den()));
    const std::int64_t limit = core::checked_mul(gap_limit, p.den());
    return dx >= -limit && dx <= limit && dy >= -limit && dy <= limit;
}

// How far p lies beyond the end point e of a line coming from `other`,
// scaled by the line's length: positive if p lies beyond e.
core::Rational beyond(IntPoint other, IntPoint e, const RationalPoint& p) {
    const IntPoint v = e - other;
    const std::int64_t dx = core::checked_sub(p.x_num(), core::checked_mul(e.x, p.den()));
    const std::int64_t dy = core::checked_sub(p.y_num(), core::checked_mul(e.y, p.den()));
    return {core::checked_add(core::checked_mul(dx, v.x), core::checked_mul(dy, v.y)), p.den()};
}

// Closes the gaps where path 1's line pixels touch but the lines do not
// (fills spread to the four neighbours of a pixel only, so lines whose
// pixels touch, even diagonally, stop a fill). Preferred, in order: extend
// the line to the other line, join two end points, or add a perpendicular
// connector.
class GapCloser {
public:
    GapCloser(const std::vector<DrawnLine>& lines, std::vector<std::optional<TopoSegment>>& segments, SceneStats& stats)
        : lines_(&lines), segments_(&segments), stats_(&stats), pixels_(index_line_pixels(lines)) {}

    void close_all() {
        for (std::size_t i = 0; i < lines_->size(); ++i) {
            const DrawnLine& line = (*lines_)[i];
            if (line.is_dot()) {
                close_dot(i, line.from);
                continue;
            }
            close_end(i, line.to, line.from, false);
            close_end(i, line.from, line.to, true);
        }
    }

private:
    // The lines other than `line` whose pixels touch (share or neighbour)
    // the pixels of `line` within contact_radius of the end point: there a
    // fill stops between the two lines.
    [[nodiscard]] std::vector<std::int32_t> neighbours(std::size_t line, pic::ScreenPoint end) const {
        std::vector<pic::ScreenPoint> own;
        const DrawnLine& drawn = (*lines_)[line];
        pic::for_each_line_pixel(drawn.from, drawn.to, [&](int x, int y) {
            if (std::abs(x - end.x) <= contact_radius && std::abs(y - end.y) <= contact_radius) {
                own.push_back({.x = x, .y = y});
            }
        });
        std::vector<std::int32_t> found;
        for (const pic::ScreenPoint p : own) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!on_screen(p.x + dx, p.y + dy)) {
                        continue;
                    }
                    const int key = ((p.y + dy) * pic::screen_width) + p.x + dx;
                    auto it = std::ranges::lower_bound(pixels_, std::pair{key, std::int32_t{0}});
                    for (; it != pixels_.end() && it->first == key; ++it) {
                        if (std::cmp_not_equal(it->second, line)) {
                            found.push_back(it->second);
                        }
                    }
                }
            }
        }
        std::ranges::stable_sort(found);
        const auto repeated = std::ranges::unique(found);
        found.erase(repeated.begin(), repeated.end());
        return found;
    }

    void close_end(std::size_t line, pic::ScreenPoint end, pic::ScreenPoint other_end, bool is_from) {
        const IntPoint e = doubled(end);
        const IntPoint other = doubled(other_end);
        const TopoSegment own = detail::make_segment(other, e);
        std::optional<RationalPoint> extension;
        core::Rational extension_length{0};
        for (const std::int32_t index : neighbours(line, end)) {
            const DrawnLine& target_line = (*lines_)[static_cast<std::size_t>(index)];
            if (target_line.is_dot()) {
                connect_to_dot(e, own, doubled(target_line.from));
                continue;
            }
            const TopoSegment target = detail::make_segment(doubled(target_line.from), doubled(target_line.to));
            if (detail::segments_touch(own, target)) {
                continue;
            }
            const auto hit = detail::line_meets_segment(other, e, target);
            if (hit && near(e, *hit) && beyond(other, e, *hit) > core::Rational{0}) {
                const core::Rational length = beyond(other, e, *hit);
                if (!extension || length > extension_length) {
                    extension = hit;
                    extension_length = length;
                }
                continue;
            }
            if (join(e, target)) {
                continue;
            }
            perpendicular(e, target);
        }
        std::optional<TopoSegment>& segment = (*segments_)[line];
        if (extension && segment) {
            (is_from ? segment->from : segment->to) = *extension;
            ++stats_->extensions;
        }
        close_to_frame(e);
    }

    // Dots (lines of one pixel) have no segment; they take part through
    // connectors to the lines and dots next to them.
    void connect_to_dot(IntPoint e, const TopoSegment& own, IntPoint dot) {
        if (dot != e && detail::perpendicular_foot(dot, own) != RationalPoint(dot)) {
            connectors_.push_back(detail::make_segment(e, dot));
            ++stats_->joins;
        }
    }

    void close_dot(std::size_t line, pic::ScreenPoint at) {
        const IntPoint e = doubled(at);
        for (const std::int32_t index : neighbours(line, at)) {
            const DrawnLine& target_line = (*lines_)[static_cast<std::size_t>(index)];
            if (target_line.is_dot()) {
                // Each pair of dots once; dots on the same pixel need nothing.
                if (std::cmp_greater(index, line) && target_line.from != at) {
                    connectors_.push_back(detail::make_segment(e, doubled(target_line.from)));
                    ++stats_->joins;
                }
                continue;
            }
            // A line ending next to the dot connects itself to it.
            const auto adjacent = [&](pic::ScreenPoint p) {
                return std::abs(p.x - at.x) <= 1 && std::abs(p.y - at.y) <= 1;
            };
            if (adjacent(target_line.from) || adjacent(target_line.to)) {
                continue;
            }
            const TopoSegment target = detail::make_segment(doubled(target_line.from), doubled(target_line.to));
            if (detail::perpendicular_foot(e, target) == RationalPoint(e)) {
                continue; // The dot lies on the line.
            }
            if (!join(e, target)) {
                perpendicular(e, target);
            }
        }
        close_to_frame(e);
    }

    // A line pixel in the outermost row or column of the picture closes
    // the way along the picture's edge, which lies half a pixel further out.
    void close_to_frame(IntPoint e) {
        constexpr std::int64_t right = 2 * std::int64_t{picture_width};
        constexpr std::int64_t bottom = 2 * std::int64_t{picture_height};
        if (e.x == 1) {
            connectors_.push_back(detail::make_segment(e, {.x = 0, .y = e.y}));
        }
        if (e.x == right - 1) {
            connectors_.push_back(detail::make_segment(e, {.x = right, .y = e.y}));
        }
        if (e.y == 1) {
            connectors_.push_back(detail::make_segment(e, {.x = e.x, .y = 0}));
        }
        if (e.y == bottom - 1) {
            connectors_.push_back(detail::make_segment(e, {.x = e.x, .y = bottom}));
        }
    }

    bool join(IntPoint e, const TopoSegment& target) {
        std::optional<IntPoint> nearest;
        std::int64_t nearest_distance = 0;
        for (const IntPoint f : {target.a, target.b}) {
            const std::int64_t distance = std::max(std::abs(f.x - e.x), std::abs(f.y - e.y));
            if (distance > 0 && distance <= gap_limit && (!nearest || distance < nearest_distance)) {
                nearest = f;
                nearest_distance = distance;
            }
        }
        if (!nearest) {
            return false;
        }
        connectors_.push_back(detail::make_segment(e, *nearest));
        ++stats_->joins;
        return true;
    }

    void perpendicular(IntPoint e, const TopoSegment& target) {
        const auto foot = detail::perpendicular_foot(e, target);
        if (!foot || !near(e, *foot) || *foot == RationalPoint(e)) {
            return;
        }
        const IntPoint d = target.b - target.a;
        const std::int64_t divisor = std::gcd(d.x, d.y);
        const IntPoint b{.x = e.x - (d.y / divisor), .y = e.y + (d.x / divisor)};
        if (std::abs(b.x) > detail::max_coordinate || std::abs(b.y) > detail::max_coordinate) {
            return;
        }
        connectors_.push_back(TopoSegment{.a = e, .b = b, .from = RationalPoint(e), .to = *foot});
        ++stats_->perpendiculars;
    }

public:
    [[nodiscard]] const std::vector<TopoSegment>& connectors() const { return connectors_; }

private:
    const std::vector<DrawnLine>* lines_;
    std::vector<std::optional<TopoSegment>>* segments_; // Empty for dots.
    std::vector<TopoSegment> connectors_;
    SceneStats* stats_;
    std::vector<std::pair<std::int32_t, std::int32_t>> pixels_;
};

std::vector<TopoSegment> picture_frame() {
    constexpr std::int64_t right = 2 * std::int64_t{picture_width};
    constexpr std::int64_t bottom = 2 * std::int64_t{picture_height};
    const std::array<IntPoint, 4> corners{
        IntPoint{.x = 0, .y = 0},
        IntPoint{.x = right, .y = 0},
        IntPoint{.x = right, .y = bottom},
        IntPoint{.x = 0, .y = bottom},
    };
    std::vector<TopoSegment> frame;
    frame.reserve(corners.size());
    for (std::size_t i = 0; i < corners.size(); ++i) {
        frame.push_back(detail::make_segment(corners[i], corners[(i + 1) % corners.size()]));
    }
    return frame;
}

// Pixels whose value differs from one of their eight neighbours.
std::vector<bool> edge_band(const std::vector<std::uint8_t>& values) {
    std::vector<bool> band(picture_pixels, false);
    for (int y = 0; y < picture_height; ++y) {
        for (int x = 0; x < picture_width; ++x) {
            const std::uint8_t value = values[picture_index(x, y)];
            for (int dy = -1; dy <= 1 && !band[picture_index(x, y)]; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx >= 0 && nx < picture_width && ny >= 0 && ny < picture_height &&
                        values[picture_index(nx, ny)] != value) {
                        band[picture_index(x, y)] = true;
                        break;
                    }
                }
            }
        }
    }
    return band;
}

// Whether a pixel belongs to a region (filled, or left as cleared) rather
// than to a line or brush drawn over it.
bool is_region_pixel(const pic::DrawTrace& trace, std::int32_t owner) {
    return owner == pic::no_owner ||
           std::holds_alternative<pic::Fill>(trace.primitives[static_cast<std::size_t>(owner)].shape);
}

// A region pixel's contribution to its face's value.
struct Sample {
    std::int32_t face = 0;
    std::uint8_t value = 0;
    bool in_band = false;

    friend auto operator<=>(const Sample&, const Sample&) = default;
};

// The most frequent value of a face's samples (the smallest on a tie), and
// whether all samples outside the edge band have it.
std::pair<std::uint8_t, bool> decide_face(std::span<const Sample> samples) {
    std::uint8_t best = 0;
    std::size_t best_count = 0;
    for (std::size_t i = 0; i < samples.size();) {
        std::size_t j = i;
        while (j < samples.size() && samples[j].value == samples[i].value) {
            ++j;
        }
        if (j - i > best_count) {
            best = samples[i].value;
            best_count = j - i;
        }
        i = j;
    }
    const bool agree = std::ranges::none_of(samples, [&](const Sample& s) { return s.value != best && !s.in_band; });
    return {best, agree};
}

void count_face(const Face& face, bool has_samples, int pixels, SceneStats& stats) {
    if (pixels == 0) {
        return;
    }
    ++stats.faces;
    if (!has_samples) {
        ++stats.thin_faces;
    } else if (face.fill == FaceFill::Blocks) {
        ++stats.mixed_faces;
    } else {
        ++stats.uniform_faces;
    }
    if (face.fill == FaceFill::Blocks) {
        stats.fallback_pixels += pixels;
    }
}

// Decides every face's value from the region pixels whose centres it
// contains. A face without any, or whose pixels disagree outside the edge
// band, falls back to blocks.
void colour_faces(const std::vector<std::int32_t>& labels, const PlaneView& view, const pic::DrawTrace& trace,
                  std::vector<Face>& faces, SceneStats& stats) {
    const std::vector<bool> band = edge_band(view.values);
    std::vector<Sample> samples;
    std::vector<int> pixel_count(faces.size(), 0);
    for (std::size_t i = 0; i < picture_pixels; ++i) {
        const std::int32_t face = labels[i];
        if (face < 0) {
            continue;
        }
        ++pixel_count[static_cast<std::size_t>(face)];
        if (is_region_pixel(trace, view.owners[i])) {
            samples.push_back(Sample{.face = face, .value = view.values[i], .in_band = band[i]});
        }
    }
    std::ranges::stable_sort(samples);
    std::vector<bool> has_samples(faces.size(), false);
    for (std::size_t start = 0; start < samples.size();) {
        const std::int32_t face = samples[start].face;
        std::size_t end = start;
        while (end < samples.size() && samples[end].face == face) {
            ++end;
        }
        const auto [value, agree] = decide_face(std::span(samples).subspan(start, end - start));
        faces[static_cast<std::size_t>(face)] =
            Face{.fill = agree ? FaceFill::Uniform : FaceFill::Blocks, .value = value};
        has_samples[static_cast<std::size_t>(face)] = true;
        start = end;
    }
    for (std::size_t f = 0; f < faces.size(); ++f) {
        if (!has_samples[f]) {
            faces[f].fill = FaceFill::Blocks;
        }
        count_face(faces[f], has_samples[f], pixel_count[f], stats);
    }
}

// The value a fallback face shows in each pixel: the map's value, except
// in pixels of lines and brushes, which take the most frequent value of
// their region neighbours (the smallest on a tie).
std::vector<std::uint8_t> block_values(const PlaneView& view, const pic::DrawTrace& trace) {
    std::vector<std::uint8_t> blocks = view.values;
    for (int y = 0; y < picture_height; ++y) {
        for (int x = 0; x < picture_width; ++x) {
            if (is_region_pixel(trace, view.owners[picture_index(x, y)])) {
                continue;
            }
            std::array<int, 256> counts{};
            int best = -1;
            for (const auto [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}}) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || nx >= picture_width || ny < 0 || ny >= picture_height ||
                    !is_region_pixel(trace, view.owners[picture_index(nx, ny)])) {
                    continue;
                }
                const std::uint8_t value = view.values[picture_index(nx, ny)];
                ++counts[value];
                if (best < 0 || counts[value] > counts[static_cast<std::size_t>(best)] ||
                    (counts[value] == counts[static_cast<std::size_t>(best)] && int{value} < best)) {
                    best = value;
                }
            }
            if (best >= 0) {
                blocks[picture_index(x, y)] = static_cast<std::uint8_t>(best);
            }
        }
    }
    return blocks;
}

Vec2 to_picture(const RationalPoint& p) {
    const auto den = static_cast<double>(core::checked_mul(2, p.den()));
    return {.x = static_cast<double>(p.x_num()) / den, .y = static_cast<double>(p.y_num()) / den};
}

std::vector<ScanEdge> scan_edges(const detail::Arrangement& arrangement) {
    std::vector<ScanEdge> edges;
    for (std::size_t i = 0; i < arrangement.half_edges.size(); i += 2) {
        const detail::HalfEdge& forward = arrangement.half_edges[i];
        const detail::HalfEdge& backward = arrangement.half_edges[i + 1];
        if (forward.direction.y == 0) {
            continue;
        }
        const bool forward_down = forward.direction.y > 0;
        const detail::HalfEdge& down = forward_down ? forward : backward;
        const detail::HalfEdge& up = forward_down ? backward : forward;
        const Vec2 top = to_picture(arrangement.vertices[static_cast<std::size_t>(down.origin)]);
        const Vec2 bottom = to_picture(arrangement.vertices[static_cast<std::size_t>(up.origin)]);
        if (bottom.y <= 0 || top.y >= picture_height) {
            continue;
        }
        edges.push_back(ScanEdge{.top = top, .bottom = bottom, .face_after = up.face});
    }
    return edges;
}

// The pixels a primitive owns at the end, with their values.
Blocks owned_blocks(const PlaneView& view, std::int32_t primitive) {
    Blocks blocks{.source = primitive, .pixels = {}, .values = {}};
    for (std::size_t i = 0; i < picture_pixels; ++i) {
        if (view.owners[i] == primitive) {
            blocks.pixels.push_back(static_cast<std::uint32_t>(i));
            blocks.values.push_back(view.values[i]);
        }
    }
    return blocks;
}

std::optional<Layer> brush_layer(const pic::Pattern& pattern, std::uint8_t value, std::int32_t source) {
    const int size = pic::pattern_size(pattern.code);
    const pic::PixelBox box = pic::pattern_box(pattern.at.x, pattern.at.y, size);
    const double left = box.left;
    const double right = std::min(box.right, pic::screen_width);
    const double top = box.top - picture_top;
    const double bottom = box.bottom - picture_top;
    if (pic::pattern_is_rectangle(pattern.code)) {
        return Box{.source = source, .left = left, .top = top, .right = right, .bottom = bottom, .value = value};
    }
    return Ellipse{
        .source = source,
        .centre = {.x = (left + right) / 2, .y = (top + bottom) / 2},
        .radius_x = (right - left) / 2,
        .radius_y = (bottom - top) / 2,
        .value = value,
    };
}

// The layers over the faces, in drawing order: visible lines as strokes,
// solid brushes as shapes, textured brushes as blocks.
std::vector<Layer> build_layers(const pic::DrawTrace& trace, const PlaneView& view, Plane plane,
                                const std::vector<DrawnLine>& lines, SceneStats& stats) {
    std::vector<int> owned(trace.primitives.size(), 0);
    for (const std::int32_t owner : view.owners) {
        if (owner != pic::no_owner) {
            ++owned[static_cast<std::size_t>(owner)];
        }
    }
    std::vector<Layer> layers;
    std::size_t next_line = 0;
    for (std::size_t i = 0; i < trace.primitives.size(); ++i) {
        const auto& primitive = trace.primitives[i];
        if (!in_plane(primitive, plane)) {
            continue;
        }
        const std::uint8_t value = value_of(primitive, plane);
        if (std::holds_alternative<pic::Line>(primitive.shape)) {
            const DrawnLine& line = lines[next_line++];
            ++stats.lines;
            if (owned[i] == 0) {
                ++stats.hidden_lines;
                continue;
            }
            Stroke stroke{.source = static_cast<std::int32_t>(i), .points = {centre(line.from)}, .value = value};
            if (!line.is_dot()) {
                stroke.points.push_back(centre(line.to));
            }
            layers.emplace_back(std::move(stroke));
            ++stats.strokes;
        } else if (const auto* pattern = std::get_if<pic::Pattern>(&primitive.shape);
                   pattern != nullptr && owned[i] > 0) {
            if (pic::pattern_is_textured(pattern->code)) {
                layers.emplace_back(owned_blocks(view, static_cast<std::int32_t>(i)));
                ++stats.textured;
            } else if (auto brush = brush_layer(*pattern, value, static_cast<std::int32_t>(i))) {
                layers.push_back(std::move(*brush));
                ++stats.brushes;
            }
        }
    }
    return layers;
}

} // namespace

BuiltScene build_scene(const pic::PictureMaps& maps, const pic::DrawTrace& trace, Plane plane) {
    BuiltScene built;
    Scene& scene = built.scene;
    scene.plane = plane;
    const PlaneView view = view_plane(maps, trace, plane);
    const std::vector<DrawnLine> lines = collect_lines(trace, plane);

    // One topology segment per line (dots have none), in line order.
    std::vector<std::optional<TopoSegment>> segments;
    segments.reserve(lines.size());
    for (const DrawnLine& line : lines) {
        if (line.is_dot()) {
            segments.emplace_back();
        } else {
            segments.emplace_back(detail::make_segment(doubled(line.from), doubled(line.to)));
        }
    }
    GapCloser closer(lines, segments, built.stats);
    closer.close_all();
    std::vector<TopoSegment> topology;
    for (const auto& segment : segments) {
        if (segment) {
            topology.push_back(*segment);
        }
    }
    topology.insert(topology.end(), closer.connectors().begin(), closer.connectors().end());
    const std::vector<TopoSegment> frame = picture_frame();
    topology.insert(topology.end(), frame.begin(), frame.end());

    const detail::Arrangement arrangement = detail::build_arrangement(topology);
    scene.faces.resize(static_cast<std::size_t>(arrangement.face_count));
    const std::vector<std::int32_t> labels = detail::label_pixel_centres(arrangement, picture_width, picture_height);
    colour_faces(labels, view, trace, scene.faces, built.stats);
    scene.block_values = block_values(view, trace);
    scene.edges = scan_edges(arrangement);
    scene.layers = build_layers(trace, view, plane, lines, built.stats);
    return built;
}

} // namespace sci::vector
