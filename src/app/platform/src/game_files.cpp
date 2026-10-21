// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/game_files.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>

namespace sci::platform {

core::Result<std::vector<resource::GameFile>> read_game_folder(const std::filesystem::path& folder) {
    std::vector<resource::GameFile> files;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream) {
            return core::make_error(core::ErrorCode::Io, std::format("cannot read {}", entry.path().string()));
        }
        files.push_back(resource::GameFile{
            .name = entry.path().filename().string(),
            .bytes =
                std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()),
        });
    }
    if (error) {
        return core::make_error(core::ErrorCode::Io, error.message());
    }
    // Directory order is unspecified; keep the input to the libraries stable.
    std::ranges::stable_sort(files,
                             [](const resource::GameFile& a, const resource::GameFile& b) { return a.name < b.name; });
    return files;
}

} // namespace sci::platform
