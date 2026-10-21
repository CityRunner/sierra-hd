// SPDX-License-Identifier: GPL-3.0-or-later

#include "game_folder.hpp"

#include "sci/core/assert.hpp"
#include "sci/core/log.hpp"
#include "sci/platform/game_files.hpp"

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

std::optional<resource::ResourceManager> open_game(const std::filesystem::path& folder) {
    auto files = platform::read_game_folder(folder);
    if (!files) {
        std::println(stderr, "cannot read {}: {}", folder.string(), files.error().message);
        return std::nullopt;
    }
    auto manager = resource::ResourceManager::open(std::move(*files));
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
