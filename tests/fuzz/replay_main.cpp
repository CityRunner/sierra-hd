// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replays fuzz inputs without libFuzzer: every file in the directories (or
// the files) given on the command line is passed once to the fuzz entry
// point. Used where the fuzzer itself is not built, so every platform runs
// the seed corpus through the same code.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <span>
#include <vector>

// NOLINTNEXTLINE(readability-identifier-naming): name fixed by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace {

bool replay_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::println(stderr, "cannot open {}", path.string());
        return false;
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    LLVMFuzzerTestOneInput(bytes.data(), bytes.size());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::filesystem::path> inputs;
    for (const char* arg : std::span(argv, static_cast<std::size_t>(argc)).subspan(1)) {
        const std::filesystem::path path(arg);
        if (std::filesystem::is_directory(path)) {
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                inputs.push_back(entry.path());
            }
        } else {
            inputs.push_back(path);
        }
    }
    // Directory iteration order is unspecified; replay in a fixed order.
    std::ranges::stable_sort(inputs);

    bool all_read = !inputs.empty();
    for (const auto& input : inputs) {
        all_read = replay_file(input) && all_read;
    }
    std::println("replayed {} inputs", inputs.size());
    return all_read ? 0 : 1;
}
