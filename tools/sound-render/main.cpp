// SPDX-License-Identifier: GPL-3.0-or-later
//
// Development tool: plays sound resources of a game folder through the
// music player and the AdLib driver (sci::vm::render_sound) and prints, per
// sound, the OPL output at its native rate (49716 Hz): frames, music timer
// calls, whether the song ended, the peak sample and the SHA-256 of the
// samples (two bytes little-endian each, left then right, as the OPL golden
// hashes take them). With --raw it also writes one sound's samples, as
// signed 16-bit little-endian stereo.
//
//   sci-sound-render <game folder> <sound number | all> [seconds] [--raw <file>]
//
// <seconds> (default 60) limits each sound; looping songs play once.

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
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <vector>

namespace {

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

bool render(const sci::resource::ResourceManager& resources, std::uint16_t number, std::uint32_t seconds,
            const char* raw) {
    const sci::vm::SoundRenderOptions options{
        .max_frames = seconds * sci::audio::Opl3::native_rate,
        .loop = 1,
        .master_volume = 11,
    };
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
    return raw == nullptr || sci::tools::write_file(raw, bytes);
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    std::vector<std::string_view> positional;
    const char* raw = nullptr;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--raw" && i + 1 < args.size()) {
            raw = args[++i];
        } else {
            positional.push_back(arg);
        }
    }
    const std::optional<unsigned> seconds = positional.size() > 2 ? parse_number(positional[2]) : 60U;
    const std::optional<unsigned> number = positional.size() > 1 ? parse_number(positional[1]) : std::nullopt;
    const bool all = positional.size() > 1 && positional[1] == "all";
    if (positional.size() < 2 || positional.size() > 3 || !seconds || (!number && !all) || (all && raw != nullptr)) {
        std::println(stderr, "usage: sci-sound-render <game folder> <sound number | all> [seconds] [--raw <file>]");
        return 2;
    }
    sci::tools::install_stderr_log();
    const auto resources = sci::tools::open_game(std::filesystem::path(positional[0]));
    if (!resources) {
        return 1;
    }
    if (!all) {
        return render(*resources, static_cast<std::uint16_t>(*number), *seconds, raw) ? 0 : 1;
    }
    bool ok = true;
    for (const sci::resource::ResourceId id : resources->list()) {
        if (id.type == sci::resource::ResourceType::Sound) {
            ok = render(*resources, id.number, *seconds, nullptr) && ok;
        }
    }
    return ok ? 0 : 1;
}
