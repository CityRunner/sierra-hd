// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden hashes of every picture's background (spec section 5.3): SHA-256
// of the HD frame (8-bit RGBA, picture area) at N = 1, 2, 5 and 10, and of
// the original-mode frame (palette indices) at N = 1. They catch changes,
// not quality, and must be the same on every machine.
//
//   sci-golden-hashes <game> <file>           writes the hashes
//   sci-golden-hashes <game> <file> --check   compares with the file
//
// One line per picture: "pic <n> <kind> <hash>" for each kind. Overlays
// are added once the scripts tell which rooms draw them (milestone 4).

#include "game_folder.hpp"
#include "picture.hpp"
#include "sci/core/sha256.hpp"
#include "sci/render/original.hpp"
#include "sci/render/rasterize.hpp"
#include "sci/vector/build_scene.hpp"
#include "thread_executor.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <print>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::array hd_scales{1, 2, 5, 10};

std::string hash(std::span<const std::uint8_t> bytes) {
    return sci::core::to_hex(sci::core::sha256(bytes));
}

std::string picture_hashes(const sci::resource::ResourceManager& manager, std::uint16_t number,
                           sci::core::Executor& executor) {
    const std::array numbers{number};
    const auto drawn = sci::tools::draw_pictures(manager, numbers);
    if (!drawn) {
        return std::format("pic {} error\n", number);
    }
    const auto visual = sci::vector::build_scene(drawn->maps, drawn->trace, sci::vector::Plane::Visual);
    std::string lines;
    for (const int scale : hd_scales) {
        const auto frame = sci::render::render_hd(visual.scene, scale, executor);
        lines += std::format("pic {} hd{} {}\n", number, scale, hash(frame.pixels));
    }
    lines += std::format("pic {} original1 {}\n", number, hash(sci::render::render_original(drawn->maps, 1)));
    return lines;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    const bool check = args.size() == 4 && std::string_view(args[3]) == "--check";
    if (args.size() != 3 && !check) {
        std::println(stderr, "usage: sci-golden-hashes <game> <file> [--check]");
        return 2;
    }
    sci::tools::install_stderr_log();
    const auto manager = sci::tools::open_game(args[1]);
    if (!manager) {
        return 1;
    }
    sci::tools::ThreadExecutor executor;
    std::string hashes;
    for (const std::uint16_t number : sci::tools::numbers_of(*manager, sci::resource::ResourceType::Pic)) {
        hashes += picture_hashes(*manager, number, executor);
    }
    const std::filesystem::path path = args[2];
    if (!check) {
        std::ofstream out(path);
        out << hashes;
        return out ? 0 : 1;
    }
    std::ifstream in(path);
    const std::string expected{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::istringstream want(expected);
    std::istringstream got(hashes);
    int differences = 0;
    while (want || got) {
        std::string a;
        std::string b;
        const bool more_wanted = static_cast<bool>(std::getline(want, a));
        const bool more_got = static_cast<bool>(std::getline(got, b));
        if ((more_wanted || more_got) && a != b) {
            std::println("expected: {}\n     got: {}", a, b);
            ++differences;
        }
    }
    std::println("{}", differences == 0 ? "all golden hashes match" : std::format("{} differences", differences));
    return differences == 0 ? 0 : 1;
}
