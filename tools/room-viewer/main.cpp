// SPDX-License-Identifier: GPL-3.0-or-later
//
// Room viewer (development tool, milestone 2): renders pictures, views and
// fonts of a game folder at 1x through sci-pic, sci-view and sci-text, and
// writes them as PNG images. Until the window layer arrives (milestone 3a)
// the viewer writes files instead of opening a window.
//
//   sci-room-viewer <game> pic <n>[+<n>...] [options] <out.png>
//       Draws the pictures in order, each further one added over the
//       previous (an overlay, as kDrawPic with the add flag).
//       --map visual|priority|control|all   (default visual; all = side by side)
//       --mirrored  --palette <0-3>  --scale <k>
//       --hd: through the vector path (milestone 3): the HD frame at scale
//       k, or with --map priority the HD priority map
//       --highlight (with --hd, in builds with SCI_DEBUG_HIGHLIGHT): fitted
//       strokes in light magenta, fallback blocks in yellow
//       --hints <directory> (with --hd): apply the hint files there
//   sci-room-viewer <game> view <n> [--scale <k>] <out.png>
//       All loops (rows) and cels; transparent pixels show the background.
//   sci-room-viewer <game> font <n> [--scale <k>] <out.png>
//       All glyphs, 16 per row.
//   sci-room-viewer <game> all <directory>
//       Every picture (all maps), view and font.
//
// Images derived from the game art are never committed (reports/images/ is
// git-ignored).

#include "game_folder.hpp"
#include "picture.hpp"
#include "png.hpp"
#include "sci/pic/draw_pic.hpp"
#include "sci/pic/parse_pic.hpp"
#include "sci/platform/picture_hints.hpp"
#include "sci/platform/thread_executor.hpp"
#include "sci/render/original.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/text/font.hpp"
#include "sci/vector/build_scene.hpp"
#include "sci/view/view.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sci::resource::ResourceId;
using sci::resource::ResourceManager;
using sci::resource::ResourceType;
using sci::tools::Image;
using sci::tools::Rgb;

constexpr Rgb background{.r = 0x30, .g = 0x30, .b = 0x30};
constexpr Rgb glyph_paper{.r = 0xFF, .g = 0xFF, .b = 0xFF};
constexpr Rgb glyph_ink{.r = 0x00, .g = 0x00, .b = 0x00};

enum class MapChoice : std::int8_t { Visual, Priority, Control, All };

struct Options {
    std::vector<std::uint16_t> pictures;
    MapChoice map = MapChoice::Visual;
    sci::pic::DrawOptions draw;
    int scale = 1;
    bool hd = false;                            // Through the vector path instead of the 1x maps.
    bool highlight = false;                     // With --hd: show replacements (SCI_DEBUG_HIGHLIGHT builds only).
    std::optional<std::filesystem::path> hints; // With --hd: the hint directory.
};

std::optional<int> parse_int(std::string_view text) {
    // Arguments are short; parse them digit by digit (no sign, no overflow).
    if (text.empty() || text.size() > 6 || !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    int value = 0;
    for (const char c : text) {
        value = (value * 10) + (c - '0');
    }
    return value;
}

void draw_scaled(Image& image, int x, int y, int scale, Rgb color) {
    for (int dy = 0; dy < scale; ++dy) {
        for (int dx = 0; dx < scale; ++dx) {
            image.set((x * scale) + dx, (y * scale) + dy, color);
        }
    }
}

std::optional<sci::resource::Resource> load(const ResourceManager& manager, ResourceType type, std::uint16_t number) {
    auto resource = manager.load(ResourceId{.type = type, .number = number});
    if (!resource) {
        std::println(stderr, "{}.{}: {}", sci::resource::resource_type_name(type), number, resource.error().message);
        return std::nullopt;
    }
    return std::move(*resource);
}

std::optional<sci::pic::PictureMaps> draw_pictures(const ResourceManager& manager, const Options& options) {
    auto drawn = sci::tools::draw_pictures(manager, options.pictures, options.draw);
    if (!drawn) {
        return std::nullopt;
    }
    return std::move(drawn->maps);
}

// The pictures through the vector path at the chosen scale: the HD frame,
// or with --map priority the HD priority map in EGA colours.
std::optional<Image> render_hd_pictures(const ResourceManager& manager, const Options& options) {
    const auto drawn = sci::tools::draw_pictures(manager, options.pictures, options.draw);
    if (!drawn) {
        return std::nullopt;
    }
    sci::platform::ThreadExecutor executor;
    const sci::platform::PictureHintSet hints(options.hints, manager, options.pictures);
    if (options.map == MapChoice::Priority) {
        const auto scene =
            sci::vector::build_scene(drawn->maps, drawn->trace, sci::vector::Plane::Priority, hints.pointers());
        const auto map = sci::render::render_priority(scene.scene, options.scale, executor);
        return sci::tools::to_image(sci::render::indices_to_rgba(map, sci::vector::picture_width * options.scale,
                                                                 sci::vector::picture_height * options.scale));
    }
    const auto scene =
        sci::vector::build_scene(drawn->maps, drawn->trace, sci::vector::Plane::Visual, hints.pointers());
#ifdef SCI_DEBUG_HIGHLIGHT
    if (options.highlight) {
        return sci::tools::to_image(
            sci::render::render_hd(sci::vector::highlight_replacements(scene.scene), options.scale, executor));
    }
#endif
    return sci::tools::to_image(sci::render::render_hd(scene.scene, options.scale, executor));
}

Image render_maps(const sci::pic::PictureMaps& maps, MapChoice choice, int scale) {
    const std::array<const std::vector<std::uint8_t>*, 3> all{&maps.visual, &maps.priority, &maps.control};
    std::vector<const std::vector<std::uint8_t>*> shown;
    if (choice == MapChoice::All) {
        shown.assign(all.begin(), all.end());
    } else {
        shown.push_back(all[static_cast<std::size_t>(choice)]);
    }
    constexpr int gap = 4;
    const int panels = static_cast<int>(shown.size());
    Image image(((sci::pic::screen_width * panels) + (gap * (panels - 1))) * scale, sci::pic::screen_height * scale,
                background);
    for (int panel = 0; panel < panels; ++panel) {
        const int left = panel * (sci::pic::screen_width + gap);
        for (int y = 0; y < sci::pic::screen_height; ++y) {
            for (int x = 0; x < sci::pic::screen_width; ++x) {
                const std::uint8_t value =
                    (*shown[static_cast<std::size_t>(panel)])[sci::pic::PictureMaps::index(x, y)];
                draw_scaled(image, left + x, y, scale, sci::tools::ega_color(value));
            }
        }
    }
    return image;
}

std::optional<Image> render_view(const ResourceManager& manager, std::uint16_t number, int scale) {
    const auto resource = load(manager, ResourceType::View, number);
    if (!resource) {
        return std::nullopt;
    }
    const auto view = sci::view::decode_view(resource->data);
    if (!view) {
        std::println(stderr, "view.{}: {}", number, view.error().message);
        return std::nullopt;
    }
    constexpr int gap = 2;
    int width = gap;
    int height = gap;
    for (const sci::view::Loop& loop : view->loops) {
        int row_width = gap;
        int row_height = 0;
        for (const sci::view::Cel& cel : loop.cels) {
            row_width += cel.width + gap;
            row_height = std::max<int>(row_height, cel.height);
        }
        width = std::max(width, row_width);
        height += row_height + gap;
    }
    Image image(width * scale, height * scale, background);
    int top = gap;
    for (const sci::view::Loop& loop : view->loops) {
        int left = gap;
        int row_height = 0;
        for (const sci::view::Cel& cel : loop.cels) {
            for (int y = 0; std::cmp_less(y, cel.height); ++y) {
                for (int x = 0; std::cmp_less(x, cel.width); ++x) {
                    const std::uint8_t color =
                        cel.pixels[(static_cast<std::size_t>(y) * cel.width) + static_cast<std::size_t>(x)];
                    if (color != cel.clear_key) {
                        draw_scaled(image, left + x, top + y, scale, sci::tools::ega_color(color));
                    }
                }
            }
            left += cel.width + gap;
            row_height = std::max<int>(row_height, cel.height);
        }
        top += row_height + gap;
    }
    return image;
}

std::optional<Image> render_font(const ResourceManager& manager, std::uint16_t number, int scale) {
    const auto resource = load(manager, ResourceType::Font, number);
    if (!resource) {
        return std::nullopt;
    }
    const auto font = sci::text::decode_font(resource->data);
    if (!font) {
        std::println(stderr, "font.{}: {}", number, font.error().message);
        return std::nullopt;
    }
    constexpr int per_row = 16;
    constexpr int gap = 2;
    int cell_width = 1;
    int cell_height = std::max<int>(1, font->line_height);
    for (const sci::text::Glyph& glyph : font->glyphs) {
        cell_width = std::max<int>(cell_width, glyph.width);
        cell_height = std::max<int>(cell_height, glyph.height);
    }
    const int rows = (static_cast<int>(font->glyphs.size()) + per_row - 1) / per_row;
    Image image((((cell_width + gap) * per_row) + gap) * scale,
                (((cell_height + gap) * std::max(rows, 1)) + gap) * scale, background);
    for (std::size_t index = 0; index < font->glyphs.size(); ++index) {
        const sci::text::Glyph& glyph = font->glyphs[index];
        const int left = gap + (static_cast<int>(index % per_row) * (cell_width + gap));
        const int top = gap + (static_cast<int>(index / per_row) * (cell_height + gap));
        for (int y = 0; y < cell_height; ++y) {
            for (int x = 0; x < cell_width; ++x) {
                const bool ink =
                    std::cmp_less(x, glyph.width) && std::cmp_less(y, glyph.height) &&
                    glyph.pixels[(static_cast<std::size_t>(y) * glyph.width) + static_cast<std::size_t>(x)] != 0;
                draw_scaled(image, left + x, top + y, scale, ink ? glyph_ink : glyph_paper);
            }
        }
    }
    return image;
}

bool save(const std::optional<Image>& image, const fs::path& path) {
    if (!image || !image->write_png(path)) {
        std::println(stderr, "cannot write {}", path.string());
        return false;
    }
    return true;
}

int render_all(const ResourceManager& manager, const fs::path& directory) {
    std::error_code error;
    fs::create_directories(directory, error);
    bool ok = true;
    for (const std::uint16_t number : sci::tools::numbers_of(manager, ResourceType::Pic)) {
        Options options;
        options.pictures = {number};
        const auto maps = draw_pictures(manager, options);
        ok = save(maps ? std::optional<Image>(render_maps(*maps, MapChoice::All, 1)) : std::nullopt,
                  directory / std::format("pic.{:03}.png", number)) &&
             ok;
    }
    for (const std::uint16_t number : sci::tools::numbers_of(manager, ResourceType::View)) {
        ok = save(render_view(manager, number, 2), directory / std::format("view.{:03}.png", number)) && ok;
    }
    for (const std::uint16_t number : sci::tools::numbers_of(manager, ResourceType::Font)) {
        ok = save(render_font(manager, number, 2), directory / std::format("font.{:03}.png", number)) && ok;
    }
    return ok ? 0 : 1;
}

std::optional<std::vector<std::uint16_t>> parse_picture_list(std::string_view text) {
    std::vector<std::uint16_t> numbers;
    while (!text.empty()) {
        const std::size_t plus = text.find('+');
        const auto number = parse_int(text.substr(0, plus));
        if (!number || *number < 0 || *number > 0x7FF) {
            return std::nullopt;
        }
        numbers.push_back(static_cast<std::uint16_t>(*number));
        text = plus == std::string_view::npos ? std::string_view{} : text.substr(plus + 1);
    }
    return numbers;
}

std::optional<MapChoice> parse_map(std::string_view text) {
    if (text == "visual") {
        return MapChoice::Visual;
    }
    if (text == "priority") {
        return MapChoice::Priority;
    }
    if (text == "control") {
        return MapChoice::Control;
    }
    if (text == "all") {
        return MapChoice::All;
    }
    return std::nullopt;
}

// --palette <0-3> or --scale <0-16>.
bool parse_number_option(std::string_view arg, std::string_view text, Options& options) {
    const auto value = parse_int(text);
    if (!value || *value < 0 || *value > 16) {
        return false;
    }
    if (arg == "--palette") {
        if (*value > 3) {
            return false;
        }
        options.draw.ega_palette = static_cast<std::uint8_t>(*value);
    } else {
        options.scale = std::max(1, *value);
    }
    return true;
}

// Parses the options between the resource number and the output path.
bool parse_options(std::span<char*> args, Options& options) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const bool has_value = i + 1 < args.size();
        if (arg == "--mirrored") {
            options.draw.mirrored = true;
        } else if (arg == "--hd") {
            options.hd = true;
#ifdef SCI_DEBUG_HIGHLIGHT
        } else if (arg == "--highlight") {
            options.highlight = true;
#endif
        } else if (arg == "--hints" && has_value) {
            options.hints = std::filesystem::path(args[++i]);
        } else if (arg == "--map" && has_value) {
            const auto map = parse_map(args[++i]);
            if (!map) {
                return false;
            }
            options.map = *map;
        } else if ((arg == "--palette" || arg == "--scale") && has_value) {
            if (!parse_number_option(arg, args[++i], options)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

void print_usage() {
    std::println(stderr, "usage: sci-room-viewer <game> pic <n>[+<n>...] [--map visual|priority|control|all]\n"
                         "                       [--mirrored] [--palette <0-3>] [--scale <k>] [--hd]\n"
                         "                       [--hints <directory>] <out.png>\n"
                         "       sci-room-viewer <game> view|font <n> [--scale <k>] <out.png>\n"
                         "       sci-room-viewer <game> all <directory>");
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() < 4) {
        print_usage();
        return 2;
    }
    sci::tools::install_stderr_log();
    const auto manager = sci::tools::open_game(args[1]);
    if (!manager) {
        return 1;
    }
    const std::string_view command = args[2];
    if (command == "all") {
        return render_all(*manager, args[3]);
    }
    if (args.size() < 5) {
        print_usage();
        return 2;
    }
    Options options;
    const auto pictures = parse_picture_list(args[3]);
    if (!pictures || pictures->empty() || !parse_options(args.subspan(4, args.size() - 5), options)) {
        print_usage();
        return 2;
    }
    options.pictures = *pictures;
    const fs::path output = args.back();
    if (command == "pic" && options.hd) {
        if (options.map != MapChoice::Visual && options.map != MapChoice::Priority) {
            print_usage();
            return 2;
        }
        return save(render_hd_pictures(*manager, options), output) ? 0 : 1;
    }
    if (command == "pic") {
        const auto maps = draw_pictures(*manager, options);
        return maps && save(render_maps(*maps, options.map, options.scale), output) ? 0 : 1;
    }
    if (command == "view" && options.pictures.size() == 1) {
        return save(render_view(*manager, options.pictures.front(), options.scale), output) ? 0 : 1;
    }
    if (command == "font" && options.pictures.size() == 1) {
        return save(render_font(*manager, options.pictures.front(), options.scale), output) ? 0 : 1;
    }
    print_usage();
    return 2;
}
