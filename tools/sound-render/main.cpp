// SPDX-License-Identifier: GPL-3.0-or-later
//
// Development tool: plays sound resources of a game folder through the
// music player and the AdLib driver (sci::vm::render_sound) and prints, per
// sound, the OPL output at its native rate (49716 Hz): frames, music timer
// calls, whether the song ended, the peak sample and the SHA-256 of the
// samples (two bytes little-endian each, left then right, as the OPL golden
// hashes take them). With --raw it also writes one sound's samples, as
// signed 16-bit little-endian stereo; with --opl the driver's OPL register
// writes, as sci-vm-trace --opl does (docs/formats/replay-and-trace.md).
//
//   sci-sound-render <game folder> <sound number | all> [seconds] [--raw <file>] [--opl <file>]
//   sci-sound-render <game folder> --golden <file> [--check]
//
// <seconds> (default 60) limits each sound; looping songs play once.
//
// --golden writes the OPL golden hashes (tests/golden/sounds.txt): every
// sound resource of the game, rendered for at most golden_seconds, one line
// each, "sound <n> adlib<seconds> <hash>". With --check it compares them
// with the file instead, prints each difference and exits with 1 if any.

#include "game_folder.hpp"
#include "sci/core/sha256.hpp"
#include "sci/resource/resource_manager.hpp"
#include "sci/vm/sound_render.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <print>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The length the golden hashes render each sound for (at most; most songs
// end sooner).
constexpr std::uint32_t golden_seconds = 20;

std::optional<unsigned> parse_number(std::string_view text) {
    unsigned value = 0;
    const std::string_view::iterator end = text.end();
    const auto [stop, error] = std::from_chars(std::to_address(text.begin()), std::to_address(end), value);
    if (error != std::errc{} || stop != std::to_address(end)) {
        return std::nullopt;
    }
    return value;
}

std::vector<std::uint8_t> sample_bytes(std::span<const std::int16_t> samples) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(samples.size() * 2);
    for (const std::int16_t sample : samples) {
        const auto value = static_cast<std::uint16_t>(sample);
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    }
    return bytes;
}

sci::vm::SoundRenderOptions render_options(std::uint32_t seconds) {
    return sci::vm::SoundRenderOptions{
        .max_frames = seconds * sci::audio::Opl3::native_rate,
        .loop = 1,
        .master_volume = 11,
        .on_opl = {},
    };
}

bool render(const sci::resource::ResourceManager& resources, std::uint16_t number, std::uint32_t seconds,
            const char* raw, const char* opl) {
    sci::vm::SoundRenderOptions options = render_options(seconds);
    std::ofstream opl_file;
    if (opl != nullptr) {
        opl_file.open(opl);
        if (!opl_file) {
            std::println(stderr, "cannot write {}", opl);
            return false;
        }
        options.on_opl = [&](std::uint32_t timer_call, std::uint16_t reg, std::uint8_t value) {
            opl_file << std::format("{} {:03x} {:02x}\n", timer_call, reg, value);
        };
    }
    const auto rendered = sci::vm::render_sound(resources, number, options);
    if (!rendered) {
        std::println(stderr, "sound {}: {}", number, rendered.error().message);
        return false;
    }
    int peak = 0;
    for (const std::int16_t sample : rendered->samples) {
        peak = std::max(peak, std::abs(static_cast<int>(sample)));
    }
    const std::vector<std::uint8_t> bytes = sample_bytes(rendered->samples);
    std::println("sound {} frames {} calls {} {} peak {} sha256 {}", number, rendered->samples.size() / 2,
                 rendered->timer_calls, rendered->ended ? "ended" : "cut", peak,
                 sci::core::to_hex(sci::core::sha256(bytes)));
    opl_file.close();
    return (raw == nullptr || sci::tools::write_file(raw, bytes)) && (opl == nullptr || opl_file);
}

// The golden hash lines of every sound resource.
std::optional<std::string> golden_hashes(const sci::resource::ResourceManager& resources) {
    std::string lines;
    for (const sci::resource::ResourceId id : resources.list()) {
        if (id.type != sci::resource::ResourceType::Sound) {
            continue;
        }
        const auto rendered = sci::vm::render_sound(resources, id.number, render_options(golden_seconds));
        if (!rendered) {
            std::println(stderr, "sound {}: {}", id.number, rendered.error().message);
            return std::nullopt;
        }
        lines += std::format("sound {} adlib{} {}\n", id.number, golden_seconds,
                             sci::core::to_hex(sci::core::sha256(sample_bytes(rendered->samples))));
    }
    return lines;
}

int golden(const sci::resource::ResourceManager& resources, const std::filesystem::path& path, bool check) {
    const std::optional<std::string> hashes = golden_hashes(resources);
    if (!hashes) {
        return 1;
    }
    if (!check) {
        std::ofstream out(path);
        out << *hashes;
        return out ? 0 : 1;
    }
    std::ifstream in(path);
    const std::string expected{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::istringstream want(expected);
    std::istringstream got(*hashes);
    int differences = 0;
    int lines = 0;
    while (want || got) {
        std::string a;
        std::string b;
        const bool more_wanted = static_cast<bool>(std::getline(want, a));
        const bool more_got = static_cast<bool>(std::getline(got, b));
        if (more_got) {
            ++lines;
        }
        if ((more_wanted || more_got) && a != b) {
            std::println("expected: {}\n     got: {}", a, b);
            ++differences;
        }
    }
    std::println("{}", differences == 0 ? std::format("all {} sound golden hashes match", lines)
                                        : std::format("{} differences", differences));
    return differences == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    std::vector<std::string_view> positional;
    const char* raw = nullptr;
    const char* opl = nullptr;
    const char* golden_file = nullptr;
    bool check = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--raw" && i + 1 < args.size()) {
            raw = args[++i];
        } else if (arg == "--opl" && i + 1 < args.size()) {
            opl = args[++i];
        } else if (arg == "--golden" && i + 1 < args.size()) {
            golden_file = args[++i];
        } else if (arg == "--check") {
            check = true;
        } else {
            positional.push_back(arg);
        }
    }
    if (golden_file != nullptr || check) {
        if (golden_file == nullptr || positional.size() != 1 || raw != nullptr || opl != nullptr) {
            std::println(stderr, "usage: sci-sound-render <game folder> --golden <file> [--check]");
            return 2;
        }
        sci::tools::install_stderr_log();
        const auto resources = sci::tools::open_game(std::filesystem::path(positional[0]));
        return resources ? golden(*resources, golden_file, check) : 1;
    }
    const std::optional<unsigned> seconds = positional.size() > 2 ? parse_number(positional[2]) : 60U;
    const std::optional<unsigned> number = positional.size() > 1 ? parse_number(positional[1]) : std::nullopt;
    const bool all = positional.size() > 1 && positional[1] == "all";
    if (positional.size() < 2 || positional.size() > 3 || !seconds || (!number && !all) || (all && (raw != nullptr || opl != nullptr))) {
        std::println(stderr, "usage: sci-sound-render <game folder> <sound number | all> [seconds] [--raw <file>] "
                             "[--opl <file>]\n"
                             "       sci-sound-render <game folder> --golden <file> [--check]");
        return 2;
    }
    sci::tools::install_stderr_log();
    const auto resources = sci::tools::open_game(std::filesystem::path(positional[0]));
    if (!resources) {
        return 1;
    }
    if (!all) {
        return render(*resources, static_cast<std::uint16_t>(*number), *seconds, raw, opl) ? 0 : 1;
    }
    bool ok = true;
    for (const sci::resource::ResourceId id : resources->list()) {
        if (id.type == sci::resource::ResourceType::Sound) {
            ok = render(*resources, id.number, *seconds, nullptr, nullptr) && ok;
        }
    }
    return ok ? 0 : 1;
}
