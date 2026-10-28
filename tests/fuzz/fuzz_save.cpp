// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads arbitrary bytes as a save file into a synthetic machine (milestone
// 8, docs/formats/save-format.md): the loader must restore it or return an
// error, never fail otherwise. The first byte chooses the mode: even, the
// rest is the file as is; odd, the rest is a file without its checksum,
// which is appended here so that mutations reach the state loader.

#include "save_game.hpp"
#include "save_world.hpp"
#include "sci/core/sha256.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <utility>

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    const std::span<const std::uint8_t> input(data, size);
    std::vector<std::uint8_t> file(input.begin() + 1, input.end());
    if ((input[0] & 1U) != 0) {
        const sci::core::Sha256Digest checksum = sci::core::sha256(file);
        file.insert(file.end(), checksum.begin(), checksum.end());
    }
    auto game = sci::test::synthetic_game();
    if (!game) {
        return 0;
    }
    sci::test::SaveWorld world(std::move(*game));
    [[maybe_unused]] const auto restored = sci::vm::read_save(*world.machine, file);
    return 0;
}
