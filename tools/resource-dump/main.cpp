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

#include "sci/core/log.hpp"
#include "sci/resource/resource_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sci::resource::GameFile;
using sci::resource::resource_type_name;

void print_log(sci::core::LogLevel level, sci::core::LogCategory category, std::string_view message) noexcept {
    std::println(stderr, "[{}:{}] {}", sci::core::to_string(category), sci::core::to_string(level), message);
}

// The regular files directly in the folder; the game does not use
// subdirectories.
std::vector<GameFile> read_game_folder(const fs::path& folder) {
    std::vector<GameFile> files;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(folder, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::ifstream stream(entry.path(), std::ios::binary);
        files.push_back(GameFile{
            .name = entry.path().filename().string(),
            .bytes =
                std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()),
        });
    }
    // Directory order is unspecified; keep the input to the library stable.
    std::ranges::stable_sort(files, [](const GameFile& a, const GameFile& b) { return a.name < b.name; });
    if (error) {
        std::println(stderr, "cannot read {}: {}", folder.string(), error.message());
    }
    return files;
}

bool write_file(const fs::path& path, std::span<const std::uint8_t> bytes) {
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::fopen(path.string().c_str(), "wb"),
                                                                  &std::fclose);
    return file != nullptr && std::fwrite(bytes.data(), 1, bytes.size(), file.get()) == bytes.size();
}

int dump(const fs::path& game_folder, const fs::path& output) {
    auto manager = sci::resource::ResourceManager::open(read_game_folder(game_folder));
    if (!manager) {
        std::println(stderr, "cannot open the game: {}", manager.error().message);
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
        ok = write_file(output / std::format("{}.{:03}", type, id.number), resource->data) && ok;
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
    sci::core::set_log_sink(&print_log);
    sci::core::set_log_category_enabled(sci::core::LogCategory::Resources, true);
    return dump(args[1], args[2]);
}
