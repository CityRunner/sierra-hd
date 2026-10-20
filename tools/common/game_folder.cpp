// SPDX-License-Identifier: GPL-3.0-or-later

#include "game_folder.hpp"

#include "sci/core/assert.hpp"
#include "sci/core/log.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <print>
#include <source_location>
#include <string_view>

namespace sci::tools {

namespace {

void print_log(core::LogLevel level, core::LogCategory category, std::string_view message) noexcept {
    std::println(stderr, "[{}:{}] {}", core::to_string(category), core::to_string(level), message);
}

void print_assertion(std::string_view expression, const std::source_location& location) noexcept {
    std::println(stderr, "assertion failed: {} ({}:{})", expression, location.file_name(), location.line());
}

} // namespace

std::vector<resource::GameFile> read_game_folder(const std::filesystem::path& folder) {
    std::vector<resource::GameFile> files;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::ifstream stream(entry.path(), std::ios::binary);
        files.push_back(resource::GameFile{
            .name = entry.path().filename().string(),
            .bytes =
                std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()),
        });
    }
    if (error) {
        std::println(stderr, "cannot read {}: {}", folder.string(), error.message());
    }
    // Directory order is unspecified; keep the input to the library stable.
    std::ranges::stable_sort(files,
                             [](const resource::GameFile& a, const resource::GameFile& b) { return a.name < b.name; });
    return files;
}

std::optional<resource::ResourceManager> open_game(const std::filesystem::path& folder) {
    auto manager = resource::ResourceManager::open(read_game_folder(folder));
    if (!manager) {
        std::println(stderr, "cannot open the game in {}: {}", folder.string(), manager.error().message);
        return std::nullopt;
    }
    return std::move(*manager);
}

bool write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    const std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::fopen(path.string().c_str(), "wb"),
                                                                  &std::fclose);
    return file != nullptr && std::fwrite(bytes.data(), 1, bytes.size(), file.get()) == bytes.size();
}

void install_stderr_log() {
    core::set_assertion_handler(&print_assertion);
    core::set_log_sink(&print_log);
    core::set_log_category_enabled(core::LogCategory::Resources, true);
}

} // namespace sci::tools
