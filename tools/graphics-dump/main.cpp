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
            output.write(name, map_bytes(maps));
        }
        if (previous) {
            const std::string name = base + ".overlay";
            sci::pic::PictureMaps maps;
            const auto first = sci::pic::draw_picture(*previous, maps, {});
            const auto second = first ? sci::pic::draw_picture(*commands, maps, {.add_to = true}) : first;
            if (second) {
                output.write(name, map_bytes(maps));
            } else {
                output.failed(name, second.error().message);
            }
        }
        previous = commands;
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
    return output.finish() ? 0 : 1;
}
