// SPDX-License-Identifier: GPL-3.0-or-later
//
// Development tool: loads every resource of a game folder through
// sci-resource and writes them in the layout of the reference ScummVM's dump
// (tools/scummvm-patches/0001-dump-resources.patch), so the two can be
// compared byte for byte (tools/resource-compare):
//
//   <output>/<type>.<number>   resource data (number with at least 3 digits)
//   <output>/manifest.txt      <type> <number> <size> <loaded type>.<loaded number> <source>
//                              or <type> <number> FAILED
//
//   sci-resource-dump <game folder> <output directory>

#include "game_folder.hpp"

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <span>
#include <string_view>

namespace {

namespace fs = std::filesystem;
using sci::resource::resource_type_name;

int dump(const fs::path& game_folder, const fs::path& output) {
    const auto manager = sci::tools::open_game(game_folder);
    if (!manager) {
        return 1;
    }
    std::error_code error;
    fs::create_directories(output, error);
    std::ofstream manifest(output / "manifest.txt");

    bool ok = static_cast<bool>(manifest);
    for (const sci::resource::ResourceId id : manager->list()) {
        const std::string_view type = resource_type_name(id.type);
        const auto resource = manager->load(id);
        if (!resource) {
            std::println(stderr, "{}.{}: {}", type, id.number, resource.error().message);
            manifest << std::format("{} {} FAILED\n", type, id.number);
            continue;
        }
        ok = sci::tools::write_file(output / std::format("{}.{:03}", type, id.number), resource->data) && ok;
        manifest << std::format("{} {} {} {}.{} {}\n", type, id.number, resource->data.size(),
                                resource_type_name(resource->loaded_id.type), resource->loaded_id.number,
                                resource->source);
    }
    manifest.close();
    return ok && manifest ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 3) {
        std::println(stderr, "usage: sci-resource-dump <game folder> <output directory>");
        return 2;
    }
    sci::tools::install_stderr_log();
    return dump(args[1], args[2]);
}
