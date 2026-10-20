// SPDX-License-Identifier: GPL-3.0-or-later
//
// Development tool: draws every picture, decodes every view and font through
// the project's libraries and writes them in the layout of the reference
// ScummVM's graphics dump (tools/scummvm-patches/0002-dump-graphics.patch),
// so the two can be compared byte for byte (tools/graphics-compare).
//
//   sci-graphics-dump <game folder> <output directory>

#include "game_folder.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/text/font.hpp"
#include "sci/view/view.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sci::resource::ResourceId;
using sci::resource::ResourceManager;
using sci::resource::ResourceType;

class Output {
public:
    explicit Output(const fs::path& directory) : directory_(directory) {
        std::error_code error;
        fs::create_directories(directory, error);
        manifest_.open(directory / "manifest.txt");
        ok_ = static_cast<bool>(manifest_);
    }

    void write(const std::string& name, std::span<const std::uint8_t> bytes) {
        ok_ = sci::tools::write_file(directory_ / name, bytes) && ok_;
        manifest_ << std::format("{} {}\n", name, bytes.size());
    }

    void failed(const std::string& name, std::string_view reason) {
        std::println(stderr, "{}: {}", name, reason);
        manifest_ << std::format("{} FAILED\n", name);
    }

    bool finish() {
        manifest_.close();
        return ok_ && static_cast<bool>(manifest_);
    }

private:
    fs::path directory_;
    std::ofstream manifest_;
    bool ok_ = false;
};

std::vector<std::uint16_t> numbers_of(const ResourceManager& manager, ResourceType type) {
    std::vector<std::uint16_t> numbers;
    for (const ResourceId id : manager.list()) {
        if (id.type == type) {
            numbers.push_back(id.number);
        }
    }
    return numbers;
}

std::vector<std::uint8_t> map_bytes(const sci::pic::PictureMaps& maps) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(maps.visual.size() * 3);
    for (const auto* map : {&maps.visual, &maps.priority, &maps.control}) {
        bytes.insert(bytes.end(), map->begin(), map->end());
    }
    return bytes;
}

// The pair map must explain the visual map: every pixel shows the colour its
// pair gives at its position. A mismatch is a bug in sci-pic.
bool pairs_consistent(const sci::pic::PictureMaps& maps) {
    for (int y = 0; y < sci::pic::screen_height; ++y) {
        for (int x = 0; x < sci::pic::screen_width; ++x) {
            const std::size_t i = sci::pic::PictureMaps::index(x, y);
            if (maps.visual[i] != sci::pic::pair_color_at(maps.pairs[i], x, y)) {
                return false;
            }
        }
    }
    return true;
}

std::optional<std::vector<sci::pic::PicCommand>> load_picture(const ResourceManager& manager, std::uint16_t number,
                                                              Output& output, const std::string& name) {
    const auto resource = manager.load(ResourceId{.type = ResourceType::Pic, .number = number});
    if (!resource) {
        output.failed(name, resource.error().message);
        return std::nullopt;
    }
    auto commands = sci::pic::parse_pic(resource->data);
    if (!commands) {
        output.failed(name, commands.error().message);
        return std::nullopt;
    }
    return std::move(*commands);
}

struct Variant {
    std::string_view name;
    sci::pic::DrawOptions options;
};

void dump_pictures(const ResourceManager& manager, Output& output) {
    const std::array variants{
        Variant{.name = "normal", .options = {}},
        Variant{.name = "mirrored", .options = {.mirrored = true}},
        Variant{.name = "palette1", .options = {.ega_palette = 1}},
        Variant{.name = "palette2", .options = {.ega_palette = 2}},
        Variant{.name = "palette3", .options = {.ega_palette = 3}},
    };
    const std::vector<std::uint16_t> numbers = numbers_of(manager, ResourceType::Pic);
    std::optional<std::vector<sci::pic::PicCommand>> previous;
    for (const std::uint16_t number : numbers) {
        const std::string base = std::format("pic.{:03}", number);
        const auto commands = load_picture(manager, number, output, base);
        if (!commands) {
            previous.reset();
            continue;
        }
        for (const Variant& variant : variants) {
            const std::string name = std::format("{}.{}", base, variant.name);
            sci::pic::PictureMaps maps;
            if (const auto drawn = sci::pic::draw_picture(*commands, maps, variant.options); !drawn) {
                output.failed(name, drawn.error().message);
                continue;
            }
            if (!pairs_consistent(maps)) {
                output.failed(name, "pair map does not match the visual map");
                continue;
            }
            output.write(name, map_bytes(maps));
        }
        if (previous) {
            const std::string name = base + ".overlay";
            sci::pic::PictureMaps maps;
            const auto first = sci::pic::draw_picture(*previous, maps, {});
            const auto second = first ? sci::pic::draw_picture(*commands, maps, {.add_to = true}) : first;
            if (second && !pairs_consistent(maps)) {
                output.failed(name, "pair map does not match the visual map");
            } else if (second) {
                output.write(name, map_bytes(maps));
            } else {
                output.failed(name, second.error().message);
            }
        }
        previous = commands;
    }
}

void put_u16(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

// view.<nnn>: u8 loop count; per loop u16 cel count; per cel u16 width,
// u16 height, i16 displaceX, u16 displaceY, u8 clear key, then the pixels.
std::vector<std::uint8_t> view_bytes(const sci::view::View& view) {
    std::vector<std::uint8_t> out;
    out.push_back(static_cast<std::uint8_t>(view.loops.size()));
    for (const sci::view::Loop& loop : view.loops) {
        put_u16(out, static_cast<std::uint32_t>(loop.cels.size()));
        for (const sci::view::Cel& cel : loop.cels) {
            put_u16(out, cel.width);
            put_u16(out, cel.height);
            put_u16(out, static_cast<std::uint16_t>(cel.displace_x));
            put_u16(out, cel.displace_y);
            out.push_back(cel.clear_key);
            out.insert(out.end(), cel.pixels.begin(), cel.pixels.end());
        }
    }
    return out;
}

// font.<nnn>: u16 character count, u8 height; per character u8 width,
// u8 height, then width * height bytes.
std::vector<std::uint8_t> font_bytes(const sci::text::Font& font) {
    std::vector<std::uint8_t> out;
    put_u16(out, static_cast<std::uint32_t>(font.glyphs.size()));
    out.push_back(font.line_height);
    for (const sci::text::Glyph& glyph : font.glyphs) {
        out.push_back(glyph.width);
        out.push_back(glyph.height);
        out.insert(out.end(), glyph.pixels.begin(), glyph.pixels.end());
    }
    return out;
}

template <typename Decoded, typename Decode, typename Serialize>
void dump_resources(const ResourceManager& manager, Output& output, ResourceType type, Decode decode,
                    Serialize serialize) {
    for (const std::uint16_t number : numbers_of(manager, type)) {
        const std::string name = std::format("{}.{:03}", sci::resource::resource_type_name(type), number);
        const auto resource = manager.load(ResourceId{.type = type, .number = number});
        if (!resource) {
            output.failed(name, resource.error().message);
            continue;
        }
        const sci::core::Result<Decoded> decoded = decode(resource->data);
        if (!decoded) {
            output.failed(name, decoded.error().message);
            continue;
        }
        output.write(name, serialize(*decoded));
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 3) {
        std::println(stderr, "usage: sci-graphics-dump <game folder> <output directory>");
        return 2;
    }
    sci::tools::install_stderr_log();
    const auto manager = sci::tools::open_game(args[1]);
    if (!manager) {
        return 1;
    }
    Output output{fs::path(args[2])};
    dump_pictures(*manager, output);
    dump_resources<sci::view::View>(*manager, output, ResourceType::View, sci::view::decode_view, view_bytes);
    dump_resources<sci::text::Font>(*manager, output, ResourceType::Font, sci::text::decode_font, font_bytes);
    return output.finish() ? 0 : 1;
}
