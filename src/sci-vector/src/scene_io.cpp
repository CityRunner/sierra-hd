// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/vector/scene_io.hpp"

#include "sci/core/byte_reader.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace sci::vector {

namespace {

constexpr std::array<std::uint8_t, 4> magic{'S', 'H', 'D', 'S'};

enum class LayerTag : std::uint8_t { Stroke = 1, Ellipse = 2, Box = 3, Blocks = 4 };

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }

    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value & 0xFFU));
        u8(static_cast<std::uint8_t>(value >> 8U));
    }

    void u32(std::uint32_t value) {
        u16(static_cast<std::uint16_t>(value & 0xFFFFU));
        u16(static_cast<std::uint16_t>(value >> 16U));
    }

    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }

    void size(std::size_t value) { u32(static_cast<std::uint32_t>(value)); }

    void f64(double value) {
        const auto bits = std::bit_cast<std::uint64_t>(value);
        u32(static_cast<std::uint32_t>(bits & 0xFFFFFFFFU));
        u32(static_cast<std::uint32_t>(bits >> 32U));
    }

    void point(Vec2 p) {
        f64(p.x);
        f64(p.y);
    }

    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

// Reads fields in order; the first problem is kept, and every later read
// returns a default value, so callers check once at the end of a group.
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : reader_(bytes) {}

    std::uint8_t u8() { return take(reader_.read_u8()); }

    std::uint16_t u16() { return take(reader_.read_u16le()); }

    std::uint32_t u32() { return take(reader_.read_u32le()); }

    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

    // A count of items of at least `item_bytes` each, no more than the bytes left can hold.
    std::size_t count(std::size_t item_bytes) {
        const std::uint32_t value = u32();
        if (value > reader_.remaining() / item_bytes) {
            fail("a count past the end");
            return 0;
        }
        return value;
    }

    double f64() {
        const std::uint64_t low = u32();
        const std::uint64_t high = u32();
        const auto value = std::bit_cast<double>(low | (high << 32U));
        if (!std::isfinite(value)) {
            fail("a coordinate that is not finite");
            return 0;
        }
        return value;
    }

    Vec2 point() {
        const double x = f64();
        const double y = f64();
        return Vec2{.x = x, .y = y};
    }

    void fail(const char* what) {
        if (!problem_) {
            problem_ = what;
        }
    }

    [[nodiscard]] bool ok() const { return !problem_; }

    [[nodiscard]] bool at_end() const { return reader_.at_end(); }

    [[nodiscard]] core::Error error() const {
        return core::Error{
            .code = core::ErrorCode::InvalidData,
            .message = std::format("analysis cache scene: {}", problem_.value_or("no problem")),
        };
    }

private:
    template <typename T>
    T take(core::Result<T> value) {
        if (!value) {
            fail("truncated");
            return T{};
        }
        return *value;
    }

    core::ByteReader reader_;
    std::optional<const char*> problem_;
};

void write_layer(Writer& out, const Layer& layer) {
    if (const auto* stroke = std::get_if<Stroke>(&layer)) {
        out.u8(static_cast<std::uint8_t>(LayerTag::Stroke));
        out.i32(stroke->source);
        out.size(stroke->points.size());
        for (const Vec2 p : stroke->points) {
            out.point(p);
        }
        out.f64(stroke->radius);
        out.u8(stroke->value);
        out.u8(stroke->fitted ? 1 : 0);
        out.size(stroke->cuts.size());
        for (const Cut& cut : stroke->cuts) {
            out.point(cut.point);
            out.point(cut.normal);
        }
    } else if (const auto* ellipse = std::get_if<Ellipse>(&layer)) {
        out.u8(static_cast<std::uint8_t>(LayerTag::Ellipse));
        out.i32(ellipse->source);
        out.point(ellipse->centre);
        out.f64(ellipse->radius_x);
        out.f64(ellipse->radius_y);
        out.u8(ellipse->value);
    } else if (const auto* box = std::get_if<Box>(&layer)) {
        out.u8(static_cast<std::uint8_t>(LayerTag::Box));
        out.i32(box->source);
        out.f64(box->left);
        out.f64(box->top);
        out.f64(box->right);
        out.f64(box->bottom);
        out.u8(box->value);
    } else {
        const auto& blocks = std::get<Blocks>(layer);
        out.u8(static_cast<std::uint8_t>(LayerTag::Blocks));
        out.i32(blocks.source);
        out.size(blocks.pixels.size());
        for (std::size_t i = 0; i < blocks.pixels.size(); ++i) {
            out.u32(blocks.pixels[i]);
            out.u8(blocks.values[i]);
        }
    }
}

Stroke read_stroke(Reader& in) {
    Stroke stroke;
    stroke.source = in.i32();
    stroke.points.resize(in.count(16));
    if (stroke.points.empty()) {
        in.fail("a stroke without points");
    }
    for (Vec2& p : stroke.points) {
        p = in.point();
    }
    stroke.radius = in.f64();
    stroke.value = in.u8();
    const std::uint8_t fitted = in.u8();
    if (fitted > 1) {
        in.fail("a bad stroke flag");
    }
    stroke.fitted = fitted == 1;
    stroke.cuts.resize(in.count(32));
    for (Cut& cut : stroke.cuts) {
        cut.point = in.point();
        cut.normal = in.point();
    }
    return stroke;
}

Blocks read_blocks(Reader& in) {
    Blocks blocks;
    blocks.source = in.i32();
    const std::size_t pixels = in.count(5);
    blocks.pixels.resize(pixels);
    blocks.values.resize(pixels);
    for (std::size_t i = 0; i < pixels && in.ok(); ++i) {
        blocks.pixels[i] = in.u32();
        blocks.values[i] = in.u8();
        if (blocks.pixels[i] >= picture_pixels || (i > 0 && blocks.pixels[i] <= blocks.pixels[i - 1])) {
            in.fail("block pixels outside the picture or not ascending");
        }
    }
    return blocks;
}

std::optional<Layer> read_layer(Reader& in) {
    switch (in.u8()) {
    case static_cast<std::uint8_t>(LayerTag::Stroke):
        return Layer{read_stroke(in)};
    case static_cast<std::uint8_t>(LayerTag::Ellipse): {
        Ellipse ellipse;
        ellipse.source = in.i32();
        ellipse.centre = in.point();
        ellipse.radius_x = in.f64();
        ellipse.radius_y = in.f64();
        ellipse.value = in.u8();
        return Layer{ellipse};
    }
    case static_cast<std::uint8_t>(LayerTag::Box): {
        Box box;
        box.source = in.i32();
        box.left = in.f64();
        box.top = in.f64();
        box.right = in.f64();
        box.bottom = in.f64();
        box.value = in.u8();
        return Layer{box};
    }
    case static_cast<std::uint8_t>(LayerTag::Blocks):
        return Layer{read_blocks(in)};
    default:
        break;
    }
    in.fail("an unknown layer");
    return std::nullopt;
}

void read_faces_and_edges(Reader& in, Scene& scene) {
    scene.faces.resize(in.count(2));
    for (Face& face : scene.faces) {
        const std::uint8_t fill = in.u8();
        face.value = in.u8();
        if (fill > 1) {
            in.fail("a bad face");
        }
        face.fill = fill == 0 ? FaceFill::Uniform : FaceFill::Blocks;
    }
    scene.edges.resize(in.count(36));
    for (ScanEdge& edge : scene.edges) {
        edge.top = in.point();
        edge.bottom = in.point();
        edge.face_after = in.i32();
        if (edge.face_after < 0 || std::cmp_greater_equal(edge.face_after, scene.faces.size()) ||
            !(edge.top.y < edge.bottom.y)) {
            in.fail("a bad edge");
        }
    }
}

} // namespace

std::vector<std::uint8_t> write_scene(const Scene& scene) {
    Writer out;
    for (const std::uint8_t byte : magic) {
        out.u8(byte);
    }
    out.u16(scene_format_version);
    out.u8(scene.plane == Plane::Visual ? 0 : 1);
    out.size(scene.faces.size());
    for (const Face& face : scene.faces) {
        out.u8(face.fill == FaceFill::Uniform ? 0 : 1);
        out.u8(face.value);
    }
    out.size(scene.edges.size());
    for (const ScanEdge& edge : scene.edges) {
        out.point(edge.top);
        out.point(edge.bottom);
        out.i32(edge.face_after);
    }
    for (const std::uint8_t value : scene.block_values) {
        out.u8(value);
    }
    out.size(scene.layers.size());
    for (const Layer& layer : scene.layers) {
        write_layer(out, layer);
    }
    return out.take();
}

core::Result<Scene> read_scene(std::span<const std::uint8_t> bytes) {
    Reader in(bytes);
    for (const std::uint8_t expected : magic) {
        if (in.u8() != expected) {
            in.fail("not a scene");
        }
    }
    if (in.u16() != scene_format_version) {
        in.fail("another format version");
    }
    if (!in.ok()) {
        return std::unexpected(in.error());
    }
    Scene scene;
    const std::uint8_t plane = in.u8();
    if (plane > 1) {
        in.fail("a bad plane");
    }
    scene.plane = plane == 0 ? Plane::Visual : Plane::Priority;
    read_faces_and_edges(in, scene);
    for (std::uint8_t& value : scene.block_values) {
        value = in.u8();
    }
    const std::size_t layers = in.count(1);
    scene.layers.reserve(layers);
    for (std::size_t i = 0; i < layers && in.ok(); ++i) {
        if (auto layer = read_layer(in)) {
            scene.layers.push_back(std::move(*layer));
        }
    }
    if (in.ok() && !in.at_end()) {
        in.fail("bytes after the scene");
    }
    if (!in.ok()) {
        return std::unexpected(in.error());
    }
    return scene;
}

} // namespace sci::vector
