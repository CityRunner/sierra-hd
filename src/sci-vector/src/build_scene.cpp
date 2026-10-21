// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vector/build_scene.hpp"

#include "curves.hpp"
#include "sci/pic/geometry.hpp"
#include "topology.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace sci::vector {

namespace {

using core::IntPoint;
using core::RationalPoint;
using detail::TopoSegment;

constexpr int picture_top = pic::picture_top;

// Chains of lines up to this long (in original pixels, either axis) may be
// fitted by curves.
constexpr int max_curve_line = 12;

// How far a fitted curve may lie from its chain (spec section 6.3: about
// 0.5 to 1 pixel). At 0.5 the stroke along the curve, 1 pixel wide, still
// touches every pixel path 1 set for the lines.
constexpr double fit_tolerance = 0.5;

// Gaps up to this far (in doubled coordinates: 2 original pixels) are
// closed when the 1x line pixels touch (spec section 6.3).
constexpr std::int64_t gap_limit = 4;

// Curves a hint asks for may lie this far from their chain (the upper end
// of the spec's range); the line check decides whether the fit is kept.
constexpr double hint_tolerance = 1.0;

// Gap hints close gaps up to this far (doubled coordinates: 8 original
// pixels).
constexpr std::int64_t hint_gap_limit = 16;

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

// Whether p lies within `limit` of the integer point e in both axes.
bool near(IntPoint e, const RationalPoint& p, std::int64_t limit_units = gap_limit) {
    const std::int64_t dx = core::checked_sub(p.x_num(), core::checked_mul(e.x, p.den()));
    const std::int64_t dy = core::checked_sub(p.y_num(), core::checked_mul(e.y, p.den()));
    const std::int64_t limit = core::checked_mul(limit_units, p.den());
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

// A connector from e perpendicular to the target line, ending on it, if the
// foot lies on the target, within `limit` of e and not at e.
std::optional<TopoSegment> perpendicular_connector(IntPoint e, const TopoSegment& target, std::int64_t limit) {
    const auto foot = detail::perpendicular_foot(e, target);
    if (!foot || !near(e, *foot, limit) || *foot == RationalPoint(e)) {
        return std::nullopt;
    }
    const IntPoint d = target.b - target.a;
    const std::int64_t divisor = std::gcd(d.x, d.y);
    const IntPoint b{.x = e.x - (d.y / divisor), .y = e.y + (d.x / divisor)};
    if (std::abs(b.x) > detail::max_coordinate || std::abs(b.y) > detail::max_coordinate) {
        return std::nullopt;
    }
    return TopoSegment{.a = e, .b = b, .from = RationalPoint(e), .to = *foot};
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
        if (auto connector = perpendicular_connector(e, target, gap_limit)) {
            connectors_.push_back(*connector);
            ++stats_->perpendiculars;
        }
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
}

// The mixed faces' region pixels and bounding boxes, in face order.
std::vector<Fallback> list_fallbacks(const std::vector<Sample>& samples, const std::vector<std::size_t>& sample_pixels,
                                     const std::vector<Face>& faces, const std::vector<bool>& has_samples) {
    std::vector<Fallback> fallbacks;
    std::int32_t current = -1;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto face = static_cast<std::size_t>(samples[i].face);
        if (!has_samples[face] || faces[face].fill != FaceFill::Blocks) {
            continue;
        }
        const int x = static_cast<int>(sample_pixels[i] % picture_width);
        const int y = static_cast<int>(sample_pixels[i] / picture_width);
        if (samples[i].face != current) {
            current = samples[i].face;
            fallbacks.push_back(Fallback{.pixels = 0, .left = x, .top = y, .right = x, .bottom = y});
        }
        Fallback& fallback = fallbacks.back();
        ++fallback.pixels;
        fallback.left = std::min(fallback.left, x);
        fallback.right = std::max(fallback.right, x);
        fallback.top = std::min(fallback.top, y);
        fallback.bottom = std::max(fallback.bottom, y);
    }
    return fallbacks;
}

// Decides every face's value from the region pixels whose centres it
// contains. A face without any, or whose pixels disagree outside the edge
// band, falls back to blocks.
void colour_faces(const std::vector<std::int32_t>& labels, const PlaneView& view, const pic::DrawTrace& trace,
                  std::vector<Face>& faces, SceneStats& stats) {
    const std::vector<bool> band = edge_band(view.values);
    std::vector<Sample> samples;
    std::vector<std::size_t> pixel_of; // Per sample.
    std::vector<int> pixel_count(faces.size(), 0);
    for (std::size_t i = 0; i < picture_pixels; ++i) {
        const std::int32_t face = labels[i];
        if (face < 0) {
            continue;
        }
        ++pixel_count[static_cast<std::size_t>(face)];
        if (is_region_pixel(trace, view.owners[i])) {
            samples.push_back(Sample{.face = face, .value = view.values[i], .in_band = band[i]});
            pixel_of.push_back(i);
        }
    }
    // Sorted by face and value; the pixel of each sample goes along.
    std::vector<std::size_t> order(samples.size());
    std::ranges::iota(order, std::size_t{0});
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return samples[i]; });
    std::vector<Sample> sorted(samples.size());
    std::vector<std::size_t> sample_pixels(samples.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        sorted[i] = samples[order[i]];
        sample_pixels[i] = pixel_of[order[i]];
    }
    samples = std::move(sorted);
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
    stats.fallbacks = list_fallbacks(samples, sample_pixels, faces, has_samples);
    for (const Fallback& fallback : stats.fallbacks) {
        stats.fallback_pixels += fallback.pixels;
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

// One scan edge, unless horizontal; `face_after` is the face of the
// half-edge running upwards.
void add_scan_edge(Vec2 from, Vec2 to, std::int32_t face_forward, std::int32_t face_backward,
                   std::vector<ScanEdge>& edges) {
    if (from.y == to.y) {
        return;
    }
    const bool down = to.y > from.y;
    const Vec2 top = down ? from : to;
    const Vec2 bottom = down ? to : from;
    if (bottom.y <= 0 || top.y >= picture_height) {
        return;
    }
    edges.push_back(ScanEdge{.top = top, .bottom = bottom, .face_after = down ? face_backward : face_forward});
}

std::vector<ScanEdge> scan_edges(const detail::Arrangement& arrangement,
                                 const std::vector<detail::FittedChain>& fitted) {
    std::vector<bool> replaced(arrangement.sources.size(), false);
    std::vector<ScanEdge> edges;
    for (const detail::FittedChain& chain : fitted) {
        for (std::size_t i = 0; i < chain.edges.size(); ++i) {
            replaced[static_cast<std::size_t>(chain.edges[i])] = true;
            const detail::HalfEdge& along = arrangement.half_edges[static_cast<std::size_t>(chain.half_edges[i])];
            const std::int32_t back = arrangement.half_edges[static_cast<std::size_t>(along.twin)].face;
            const auto& piece = chain.pieces[i];
            for (std::size_t k = 0; k + 1 < piece.size(); ++k) {
                add_scan_edge(piece[k], piece[k + 1], along.face, back, edges);
            }
        }
    }
    for (std::size_t edge = 0; edge < replaced.size(); ++edge) {
        if (replaced[edge]) {
            continue;
        }
        const detail::HalfEdge& forward = arrangement.half_edges[2 * edge];
        const detail::HalfEdge& backward = arrangement.half_edges[(2 * edge) + 1];
        add_scan_edge(detail::to_picture(arrangement.vertices[static_cast<std::size_t>(forward.origin)]),
                      detail::to_picture(arrangement.vertices[static_cast<std::size_t>(backward.origin)]), forward.face,
                      backward.face, edges);
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

// How many pixels each primitive owns at the end.
std::vector<int> owned_pixels(const pic::DrawTrace& trace, const PlaneView& view) {
    std::vector<int> owned(trace.primitives.size(), 0);
    for (const std::int32_t owner : view.owners) {
        if (owner != pic::no_owner) {
            ++owned[static_cast<std::size_t>(owner)];
        }
    }
    return owned;
}

// The hints that refer to lines of this plane, with their lines as line
// indices.
struct HintChain {
    int picture = 0;
    const ChainHint* hint = nullptr;
    std::vector<std::size_t> lines;
};

struct HintGap {
    int picture = 0;
    const GapHint* hint = nullptr;
    std::size_t from = 0;
    std::size_t to = 0;
};

struct PlaneHints {
    std::vector<HintChain> chains;
    std::vector<HintGap> gaps;
};

std::string_view kind_name(HintPrimitive primitive) {
    switch (primitive) {
    case HintPrimitive::Line:
        return "a line";
    case HintPrimitive::Arc:
        return "arcs";
    case HintPrimitive::Spline:
        return "a spline";
    case HintPrimitive::Ellipse:
        return "an ellipse";
    }
    return "a spline";
}

detail::CurveKind curve_kind(HintPrimitive primitive) {
    switch (primitive) {
    case HintPrimitive::Line:
        return detail::CurveKind::Line;
    case HintPrimitive::Arc:
        return detail::CurveKind::Arc;
    case HintPrimitive::Spline:
        return detail::CurveKind::Spline;
    case HintPrimitive::Ellipse:
        return detail::CurveKind::Ellipse;
    }
    return detail::CurveKind::Spline;
}

// Finds the lines that hints refer to by picture and command.
class HintLines {
public:
    HintLines(const pic::DrawTrace& trace, const std::vector<DrawnLine>& lines) {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto& primitive = trace.primitives[lines[i].primitive];
            in_plane_.emplace_back(key(primitive.picture, primitive.command), i);
        }
        for (const auto& primitive : trace.primitives) {
            if (std::holds_alternative<pic::Line>(primitive.shape)) {
                drawn_.push_back(key(primitive.picture, primitive.command));
            }
        }
        std::ranges::stable_sort(in_plane_);
        std::ranges::stable_sort(drawn_);
    }

    // The index of the line in this plane.
    [[nodiscard]] std::optional<std::size_t> line(std::size_t picture, LineRef command) const {
        const std::uint64_t k = key(picture, command);
        const auto it = std::ranges::lower_bound(in_plane_, std::pair{k, std::size_t{0}});
        if (it == in_plane_.end() || it->first != k) {
            return std::nullopt;
        }
        return it->second;
    }

    // Whether the command draws a line, in any plane.
    [[nodiscard]] bool drawn(std::size_t picture, LineRef command) const {
        return std::ranges::binary_search(drawn_, key(picture, command));
    }

private:
    static std::uint64_t key(std::size_t picture, LineRef command) {
        return (static_cast<std::uint64_t>(picture) << 32U) | command;
    }

    std::vector<std::pair<std::uint64_t, std::size_t>> in_plane_;
    std::vector<std::uint64_t> drawn_;
};

void reject_hint(SceneStats& stats, int picture, const std::string& id, std::string reason) {
    stats.rejected_hints.push_back(HintOutcome{.picture = picture, .id = id, .detail = std::move(reason)});
}

// A chain hint's lines in this plane; nothing if it has fewer than two
// here, or a command that draws no line at all.
std::optional<HintChain> resolve_chain(const HintLines& find, std::size_t p, const PictureHints& picture,
                                       const ChainHint& chain, SceneStats& stats) {
    const auto missing = std::ranges::find_if(chain.lines, [&](LineRef ref) { return !find.drawn(p, ref); });
    if (missing != chain.lines.end()) {
        reject_hint(stats, picture.picture, chain.id, std::format("command {} draws no line", *missing));
        return std::nullopt;
    }
    HintChain found{.picture = picture.picture, .hint = &chain, .lines = {}};
    for (const LineRef ref : chain.lines) {
        if (const auto line = find.line(p, ref)) {
            found.lines.push_back(*line);
        }
    }
    if (found.lines.size() < 2) {
        return std::nullopt;
    }
    return found;
}

std::optional<HintGap> resolve_gap(const HintLines& find, std::size_t p, const PictureHints& picture,
                                   const GapHint& gap, SceneStats& stats) {
    for (const LineRef ref : {gap.from, gap.to}) {
        if (!find.drawn(p, ref)) {
            reject_hint(stats, picture.picture, gap.id, std::format("command {} draws no line", ref));
            return std::nullopt;
        }
    }
    const auto from = find.line(p, gap.from);
    const auto to = find.line(p, gap.to);
    if (!from || !to) {
        return std::nullopt;
    }
    return HintGap{.picture = picture.picture, .hint = &gap, .from = *from, .to = *to};
}

PlaneHints resolve_hints(const pic::DrawTrace& trace, const std::vector<DrawnLine>& lines,
                         std::span<const PictureHints* const> hints, SceneStats& stats) {
    const HintLines find(trace, lines);
    PlaneHints resolved;
    for (std::size_t p = 0; p < hints.size() && p < trace.pictures; ++p) {
        if (hints[p] == nullptr) {
            continue;
        }
        for (const ChainHint& chain : hints[p]->chains) {
            if (auto found = resolve_chain(find, p, *hints[p], chain, stats)) {
                resolved.chains.push_back(std::move(*found));
            }
        }
        for (const GapHint& gap : hints[p]->gaps) {
            if (auto found = resolve_gap(find, p, *hints[p], gap, stats)) {
                resolved.gaps.push_back(*found);
            }
        }
    }
    return resolved;
}

// The connector a gap hint asks for: from the line's end to the nearest
// point of the other line, if that is within hint_gap_limit and not
// already touching.
std::optional<TopoSegment> gap_connector(const HintGap& gap, const std::vector<DrawnLine>& lines) {
    const DrawnLine& from = lines[gap.from];
    const DrawnLine& to = lines[gap.to];
    const IntPoint e = doubled(gap.hint->end == LineEnd::Start ? from.from : from.to);
    const auto chebyshev = [&](IntPoint p) { return std::max(std::abs(p.x - e.x), std::abs(p.y - e.y)); };
    if (!to.is_dot()) {
        const TopoSegment target = detail::make_segment(doubled(to.from), doubled(to.to));
        if (detail::perpendicular_foot(e, target) == RationalPoint(e)) {
            return std::nullopt;
        }
        if (auto connector = perpendicular_connector(e, target, hint_gap_limit)) {
            return connector;
        }
    }
    const IntPoint a = doubled(to.from);
    const IntPoint b = doubled(to.to);
    const IntPoint nearest = chebyshev(a) <= chebyshev(b) ? a : b;
    if (nearest == e || chebyshev(nearest) > hint_gap_limit) {
        return std::nullopt;
    }
    return detail::make_segment(e, nearest);
}

// The faces of a topology, coloured, with the face counts of the stats.
struct Partition {
    detail::Arrangement arrangement;
    std::vector<Face> faces;
    SceneStats stats;
};

Partition partition(std::span<const TopoSegment> topology, const PlaneView& view, const pic::DrawTrace& trace) {
    Partition result{.arrangement = detail::build_arrangement(topology), .faces = {}, .stats = {}};
    result.faces.resize(static_cast<std::size_t>(result.arrangement.face_count));
    const std::vector<std::int32_t> labels =
        detail::label_pixel_centres(result.arrangement, picture_width, picture_height);
    colour_faces(labels, view, trace, result.faces, result.stats);
    return result;
}

// Whether a partition has no more mixed faces and 1x blocks than another
// (the rule gap hints must keep).
bool no_worse(const Partition& candidate, const Partition& base) {
    return candidate.stats.mixed_faces <= base.stats.mixed_faces &&
           candidate.stats.fallback_pixels <= base.stats.fallback_pixels;
}

// Partitions the topology, closing the gaps that hints ask for where that
// keeps the rule: all at once if they keep it together, else one by one.
Partition close_hinted_gaps(std::vector<TopoSegment> topology, const PlaneHints& hints,
                            const std::vector<DrawnLine>& lines, const PlaneView& view, const pic::DrawTrace& trace,
                            SceneStats& stats) {
    Partition base = partition(topology, view, trace);
    std::vector<std::pair<const HintGap*, TopoSegment>> candidates;
    for (const HintGap& gap : hints.gaps) {
        if (auto connector = gap_connector(gap, lines)) {
            candidates.emplace_back(&gap, *connector);
        } else {
            reject_hint(stats, gap.picture, gap.hint->id, "the lines already touch, or are too far apart");
        }
    }
    if (candidates.empty()) {
        return base;
    }
    std::vector<TopoSegment> all = topology;
    for (const auto& candidate : candidates) {
        all.push_back(candidate.second);
    }
    if (Partition closed = partition(all, view, trace); no_worse(closed, base)) {
        stats.hint_gaps += static_cast<int>(candidates.size());
        for (const auto& candidate : candidates) {
            stats.applied_hints.push_back(HintOutcome{
                .picture = candidate.first->picture,
                .id = candidate.first->hint->id,
                .detail = "gap closed",
            });
        }
        return closed;
    }
    for (const auto& [gap, connector] : candidates) {
        topology.push_back(connector);
        if (Partition closed = partition(topology, view, trace); no_worse(closed, base)) {
            base = std::move(closed);
            ++stats.hint_gaps;
            stats.applied_hints.push_back(
                HintOutcome{.picture = gap->picture, .id = gap->hint->id, .detail = "gap closed"});
        } else {
            topology.pop_back();
            reject_hint(stats, gap->picture, gap->hint->id, "closing it adds mixed faces or 1x blocks");
        }
    }
    return base;
}

void copy_face_stats(const SceneStats& from, SceneStats& to) {
    to.faces = from.faces;
    to.uniform_faces = from.uniform_faces;
    to.thin_faces = from.thin_faces;
    to.mixed_faces = from.mixed_faces;
    to.fallback_pixels = from.fallback_pixels;
    to.fallbacks = from.fallbacks;
}

// Whether a stroke along the points, 1 pixel wide, covers one of the
// pixel's 16 x 16 samples (the acceptance check's line rule, spec section
// 6.4).
bool stroke_touches(const std::vector<Vec2>& points, int x, int y) {
    constexpr int samples = 16;
    constexpr double radius = 0.5;
    const auto within = [&](Vec2 p) {
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            const Vec2 a = points[i];
            const Vec2 d{.x = points[i + 1].x - a.x, .y = points[i + 1].y - a.y};
            const double length_squared = (d.x * d.x) + (d.y * d.y);
            const double t = length_squared == 0
                                 ? 0
                                 : std::clamp((((p.x - a.x) * d.x) + ((p.y - a.y) * d.y)) / length_squared, 0.0, 1.0);
            const double ex = p.x - a.x - (t * d.x);
            const double ey = p.y - a.y - (t * d.y);
            if ((ex * ex) + (ey * ey) <= radius * radius) {
                return true;
            }
        }
        return false;
    };
    if (within({.x = x + 0.5, .y = y + 0.5})) {
        return true;
    }
    for (int sy = 0; sy < samples; ++sy) {
        for (int sx = 0; sx < samples; ++sx) {
            if (within({.x = x + ((sx + 0.5) / samples), .y = y + ((sy + 0.5) / samples)})) {
                return true;
            }
        }
    }
    return false;
}

// Replaces the chains of short visible lines that imitate curves.
// Lines of a chain hint (hint_of_line, the hint's index, or -1) take part
// whether short or not; failed_hints lists the hints whose fit failed.
std::vector<detail::FittedChain> fit_curves(const detail::Arrangement& arrangement, const std::vector<DrawnLine>& lines,
                                            const std::vector<std::optional<TopoSegment>>& segments,
                                            const std::vector<int>& owned, std::span<const std::int32_t> hint_of_line,
                                            std::span<const detail::CurveKind> hint_kinds,
                                            std::vector<std::int32_t>& failed_hints, SceneStats& stats) {
    std::vector<detail::ChainLine> chain_lines(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const DrawnLine& line = lines[i];
        const IntPoint a = doubled(line.from);
        const IntPoint b = doubled(line.to);
        // Lines extended to close a gap end on another line: they are not
        // part of a curve there.
        const std::optional<TopoSegment>& segment = segments[i];
        const bool extended =
            !segment.has_value() || segment->from != RationalPoint(a) || segment->to != RationalPoint(b);
        const bool short_line =
            std::abs(line.from.x - line.to.x) <= max_curve_line && std::abs(line.from.y - line.to.y) <= max_curve_line;
        const bool visible = !line.is_dot() && owned[lines[i].primitive] > 0;
        chain_lines[i] = detail::ChainLine{
            .usable = visible && !extended && short_line,
            .hint = visible ? hint_of_line[i] : -1,
        };
    }
    detail::CurveStats curve_stats;
    auto fitted = detail::fit_chains(arrangement, chain_lines, fit_tolerance, hint_kinds, hint_tolerance, curve_stats);
    failed_hints = std::move(curve_stats.failed_hints);
    stats.straightened = curve_stats.lines;
    stats.arcs = curve_stats.arcs;
    stats.splines = curve_stats.splines;
    stats.ellipses = curve_stats.ellipses;
    std::vector<bool> fitted_line(lines.size(), false);
    for (const detail::FittedChain& chain : fitted) {
        for (const std::int32_t edge : chain.edges) {
            fitted_line[static_cast<std::size_t>(arrangement.sources[static_cast<std::size_t>(edge)])] = true;
        }
    }
    stats.fitted_lines = static_cast<int>(std::ranges::count(fitted_line, true));
    stats.rejected_fits = curve_stats.rejected;
    return fitted;
}

// The points of an edge of a fitted line, from the end nearer the line's
// start: its fitted piece, or the straight edge.
std::vector<Vec2> edge_points(const detail::Arrangement& arrangement, std::size_t edge, const std::vector<Vec2>* piece,
                              bool piece_reversed, const DrawnLine& line) {
    std::vector<Vec2> points;
    if (piece != nullptr) {
        points = *piece;
        if (piece_reversed) {
            std::ranges::reverse(points);
        }
    } else {
        const auto vertex = [&](std::size_t half_edge) {
            return detail::to_picture(
                arrangement.vertices[static_cast<std::size_t>(arrangement.half_edges[half_edge].origin)]);
        };
        points = {vertex(2 * edge), vertex((2 * edge) + 1)};
    }
    const Vec2 from = centre(line.from);
    const Vec2 to = centre(line.to);
    const auto along = [&](Vec2 p) { return ((p.x - from.x) * (to.x - from.x)) + ((p.y - from.y) * (to.y - from.y)); };
    if (along(points.back()) < along(points.front())) {
        std::ranges::reverse(points);
    }
    return points;
}

// A line's stroke from its parts in order along the line. Parts left out
// (edges shared with overlapping segments) are straight pieces of the line:
// joining the parts restores them.
std::vector<Vec2> join_parts(std::vector<std::vector<Vec2>> parts, const DrawnLine& line) {
    const Vec2 from = centre(line.from);
    const Vec2 to = centre(line.to);
    const auto along = [&](const std::vector<Vec2>& part) {
        return ((part.front().x - from.x) * (to.x - from.x)) + ((part.front().y - from.y) * (to.y - from.y));
    };
    std::ranges::stable_sort(parts, {}, along);
    std::vector<Vec2> points{from};
    for (const auto& part : parts) {
        for (const Vec2 p : part) {
            if (points.back() != p) {
                points.push_back(p);
            }
        }
    }
    if (points.back() != to) {
        points.push_back(to);
    }
    return points;
}

// The stroke of every line with fitted edges: the pieces of its edges
// (fitted curves, or straight where not fitted), from the line's start to
// its end. Empty for lines without fitted edges.
std::vector<std::vector<Vec2>> stroke_curves(const detail::Arrangement& arrangement,
                                             const std::vector<detail::FittedChain>& fitted,
                                             const std::vector<DrawnLine>& lines) {
    // The fitted piece of every fitted edge, and whether the chain runs
    // against the edge's own direction (from half-edge 2i's origin).
    std::vector<const std::vector<Vec2>*> piece_of(arrangement.sources.size(), nullptr);
    std::vector<bool> reversed(arrangement.sources.size(), false);
    std::vector<bool> has_fit(lines.size(), false);
    for (const detail::FittedChain& chain : fitted) {
        for (std::size_t i = 0; i < chain.edges.size(); ++i) {
            const auto edge = static_cast<std::size_t>(chain.edges[i]);
            piece_of[edge] = &chain.pieces[i];
            reversed[edge] = chain.half_edges[i] % 2 != 0;
            has_fit[static_cast<std::size_t>(arrangement.sources[edge])] = true;
        }
    }
    std::vector<std::vector<std::vector<Vec2>>> parts(lines.size());
    for (std::size_t edge = 0; edge < arrangement.sources.size(); ++edge) {
        const std::int32_t source = arrangement.sources[edge];
        if (source >= 0 && has_fit[static_cast<std::size_t>(source)]) {
            const auto line = static_cast<std::size_t>(source);
            parts[line].push_back(edge_points(arrangement, edge, piece_of[edge], reversed[edge], lines[line]));
        }
    }
    std::vector<std::vector<Vec2>> curves(lines.size());
    for (std::size_t line = 0; line < lines.size(); ++line) {
        if (has_fit[line]) {
            curves[line] = join_parts(std::move(parts[line]), lines[line]);
        }
    }
    return curves;
}

// Fits the curves, with the chain hints that keep the line check: every
// pixel a hinted line owns at 1x stays touched by its stroke. A hint that
// fails is dropped, and its lines are fitted automatically.
struct Curves {
    std::vector<detail::FittedChain> fitted;
    std::vector<std::vector<Vec2>> strokes; // Per line, as stroke_curves.
};

// The pixels each line of a chain hint owns at 1x, per line.
std::vector<std::vector<std::size_t>> hinted_line_pixels(const std::vector<DrawnLine>& lines,
                                                         const std::vector<std::int32_t>& hint_of_line,
                                                         const PlaneView& view) {
    std::vector<std::vector<std::size_t>> pixels(lines.size());
    std::vector<std::int32_t> line_of_primitive;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (hint_of_line[i] < 0) {
            continue;
        }
        if (lines[i].primitive >= line_of_primitive.size()) {
            line_of_primitive.resize(lines[i].primitive + 1, -1);
        }
        line_of_primitive[lines[i].primitive] = static_cast<std::int32_t>(i);
    }
    for (std::size_t i = 0; i < picture_pixels && !line_of_primitive.empty(); ++i) {
        const std::int32_t owner = view.owners[i];
        if (owner < 0 || std::cmp_greater_equal(owner, line_of_primitive.size())) {
            continue;
        }
        if (const std::int32_t line = line_of_primitive[static_cast<std::size_t>(owner)]; line >= 0) {
            pixels[static_cast<std::size_t>(line)].push_back(i);
        }
    }
    return pixels;
}

// Whether every pixel a line owns is touched by its stroke (lines without
// a fitted stroke keep their own).
bool keeps_line_pixels(const std::vector<std::size_t>& pixels, const std::vector<Vec2>& stroke) {
    return stroke.empty() || std::ranges::all_of(pixels, [&](std::size_t i) {
               return stroke_touches(stroke, static_cast<int>(i % picture_width), static_cast<int>(i / picture_width));
           });
}

// Why each chain hint fails after a fitting round (empty where it holds),
// and whether it was fitted at all.
struct HintCheck {
    std::vector<std::string> reasons;
    std::vector<bool> fitted;
};

HintCheck check_hint_chains(const PlaneHints& hints, const Curves& curves, const std::vector<std::int32_t>& failed,
                            const std::vector<std::vector<std::size_t>>& pixels) {
    HintCheck check{
        .reasons = std::vector<std::string>(hints.chains.size()),
        .fitted = std::vector<bool>(hints.chains.size(), false),
    };
    for (const std::int32_t h : failed) {
        check.reasons[static_cast<std::size_t>(h)] = "no such curve within 1 pixel that keeps the faces";
    }
    for (const detail::FittedChain& chain : curves.fitted) {
        if (chain.hint >= 0) {
            check.fitted[static_cast<std::size_t>(chain.hint)] = true;
        }
    }
    for (std::size_t h = 0; h < hints.chains.size(); ++h) {
        if (check.reasons[h].empty() && check.fitted[h] &&
            !std::ranges::all_of(hints.chains[h].lines, [&](std::size_t line) {
                return keeps_line_pixels(pixels[line], curves.strokes[line]);
            })) {
            check.reasons[h] = "the curve misses pixels of its lines";
        }
    }
    return check;
}

Curves fit_with_hints(const detail::Arrangement& arrangement, const std::vector<DrawnLine>& lines,
                      const std::vector<std::optional<TopoSegment>>& segments, const std::vector<int>& owned,
                      const PlaneView& view, const PlaneHints& hints, SceneStats& stats) {
    std::vector<std::int32_t> hint_of_line(lines.size(), -1);
    std::vector<detail::CurveKind> kinds;
    for (std::size_t h = 0; h < hints.chains.size(); ++h) {
        for (const std::size_t line : hints.chains[h].lines) {
            hint_of_line[line] = static_cast<std::int32_t>(h);
        }
        kinds.push_back(curve_kind(hints.chains[h].hint->primitive));
    }
    const std::vector<std::vector<std::size_t>> pixels = hinted_line_pixels(lines, hint_of_line, view);
    std::vector<bool> enabled(hints.chains.size(), true);
    const auto drop = [&](std::size_t h, std::string reason) {
        enabled[h] = false;
        for (const std::size_t line : hints.chains[h].lines) {
            hint_of_line[line] = -1;
        }
        reject_hint(stats, hints.chains[h].picture, hints.chains[h].hint->id, std::move(reason));
    };
    // Each round drops at least one hint, or ends.
    while (true) {
        std::vector<std::int32_t> failed;
        Curves curves;
        curves.fitted = fit_curves(arrangement, lines, segments, owned, hint_of_line, kinds, failed, stats);
        curves.strokes = stroke_curves(arrangement, curves.fitted, lines);
        const HintCheck check = check_hint_chains(hints, curves, failed, pixels);
        bool dropped = false;
        for (std::size_t h = 0; h < hints.chains.size(); ++h) {
            if (enabled[h] && !check.reasons[h].empty()) {
                drop(h, check.reasons[h]);
                dropped = true;
            }
        }
        if (dropped) {
            continue;
        }
        for (std::size_t h = 0; h < hints.chains.size(); ++h) {
            if (enabled[h] && check.fitted[h]) {
                ++stats.hint_chains;
                stats.applied_hints.push_back(HintOutcome{
                    .picture = hints.chains[h].picture,
                    .id = hints.chains[h].hint->id,
                    .detail = std::format("{} lines fitted as {}", hints.chains[h].lines.size(),
                                          kind_name(hints.chains[h].hint->primitive)),
                });
            } else if (enabled[h]) {
                drop(h, "its lines form no chain");
            }
        }
        return curves;
    }
}

// The layers over the faces, in drawing order: visible lines as strokes,
// solid brushes as shapes, textured brushes as blocks.
std::vector<Layer> build_layers(const pic::DrawTrace& trace, const PlaneView& view, Plane plane,
                                const std::vector<DrawnLine>& lines, const std::vector<int>& owned,
                                const std::vector<std::vector<Vec2>>& curves, SceneStats& stats) {
    std::vector<Layer> layers;
    std::size_t next_line = 0;
    for (std::size_t i = 0; i < trace.primitives.size(); ++i) {
        const auto& primitive = trace.primitives[i];
        if (!in_plane(primitive, plane)) {
            continue;
        }
        const std::uint8_t value = value_of(primitive, plane);
        if (std::holds_alternative<pic::Line>(primitive.shape)) {
            const std::size_t index = next_line++;
            const DrawnLine& line = lines[index];
            ++stats.lines;
            if (owned[i] == 0) {
                ++stats.hidden_lines;
                continue;
            }
            Stroke stroke{.source = static_cast<std::int32_t>(i), .points = {centre(line.from)}, .value = value};
            if (!curves[index].empty()) {
                stroke.points = curves[index];
                stroke.fitted = true;
            } else if (!line.is_dot()) {
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

BuiltScene build_scene(const pic::PictureMaps& maps, const pic::DrawTrace& trace, Plane plane,
                       std::span<const PictureHints* const> hints) {
    BuiltScene built;
    Scene& scene = built.scene;
    scene.plane = plane;
    const PlaneView view = view_plane(maps, trace, plane);
    const std::vector<DrawnLine> lines = collect_lines(trace, plane);

    // One topology segment per line (dots have none), in line order; its
    // source is the line's index.
    std::vector<std::optional<TopoSegment>> segments;
    segments.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].is_dot()) {
            segments.emplace_back();
        } else {
            segments.emplace_back(
                detail::make_segment(doubled(lines[i].from), doubled(lines[i].to), static_cast<std::int32_t>(i)));
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

    const PlaneHints plane_hints = resolve_hints(trace, lines, hints, built.stats);
    Partition parts = close_hinted_gaps(std::move(topology), plane_hints, lines, view, trace, built.stats);
    copy_face_stats(parts.stats, built.stats);
    scene.faces = std::move(parts.faces);
    scene.block_values = block_values(view, trace);

    const std::vector<int> owned = owned_pixels(trace, view);
    const Curves curves = fit_with_hints(parts.arrangement, lines, segments, owned, view, plane_hints, built.stats);
    scene.edges = scan_edges(parts.arrangement, curves.fitted);
    scene.layers = build_layers(trace, view, plane, lines, owned, curves.strokes, built.stats);
    return built;
}

} // namespace sci::vector

#ifdef SCI_DEBUG_HIGHLIGHT
namespace sci::vector {

Scene highlight_replacements(Scene scene) {
    constexpr std::uint8_t fitted = 0xDD;   // Light magenta.
    constexpr std::uint8_t fallback = 0xEE; // Yellow.
    for (Layer& layer : scene.layers) {
        if (auto* stroke = std::get_if<Stroke>(&layer); stroke != nullptr && stroke->fitted) {
            stroke->value = fitted;
        }
    }
    // Only fallback faces read the block values.
    std::ranges::fill(scene.block_values, fallback);
    return scene;
}

} // namespace sci::vector
#endif
