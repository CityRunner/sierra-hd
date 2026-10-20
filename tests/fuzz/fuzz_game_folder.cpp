// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds a game folder from the input and loads every resource in it,
// covering map and volume format checks, map parsing, patch-file discovery
// and loading. Input: a sequence of files, each a name length (1 byte), the
// name, a size (16-bit little-endian) and the content; a truncated last file
// is cut to the bytes available.

#include "sci/core/assert.hpp"
#include "sci/core/byte_reader.hpp"
#include "sci/resource/resource_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

std::vector<sci::resource::GameFile> parse_files(std::span<const std::uint8_t> input) {
    std::vector<sci::resource::GameFile> files;
    sci::core::ByteReader reader(input);
    while (const auto name_length = reader.read_u8()) {
        const auto name = reader.read_bytes(std::min<std::size_t>(*name_length, reader.remaining()));
        const auto size = reader.read_u16le();
        if (!name || !size) {
            break;
        }
        const auto content = reader.read_bytes(std::min<std::size_t>(*size, reader.remaining()));
        if (!content) {
            break;
        }
        files.push_back(sci::resource::GameFile{
            .name = std::string(name->begin(), name->end()),
            .bytes = std::vector<std::uint8_t>(content->begin(), content->end()),
        });
    }
    return files;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto manager = sci::resource::ResourceManager::open(parse_files(std::span(data, size)));
    if (!manager) {
        return 0;
    }
    for (const sci::resource::ResourceId id : manager->list()) {
        const auto resource = manager->load(id);
        // Every listed resource exists; loading may still fail on bad data.
        SCI_ASSERT(resource || resource.error().code != sci::core::ErrorCode::NotFound);
    }
    return 0;
}
