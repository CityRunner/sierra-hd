// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/config.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <memory>
#include <optional>
#include <string>

namespace sci::platform {

namespace {

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

// A value without a trailing comment.
std::string_view value_of(std::string_view text) {
    const auto comment = text.find_first_of(";#");
    return trim(comment == std::string_view::npos ? text : text.substr(0, comment));
}

std::optional<int> parse_int(std::string_view text) {
    int value = 0;
    const auto [end, error] = std::from_chars(std::to_address(text.begin()), std::to_address(text.end()), value);
    if (error != std::errc{} || end != std::to_address(text.end())) {
        return std::nullopt;
    }
    return value;
}

// Sets one key; the problem, if the value is not valid.
std::optional<std::string> apply(ParsedConfig& parsed, std::string_view key, std::string_view value) {
    Config& config = parsed.config;
    const auto to_path = [](std::string_view text) {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    };
    if (key == "format") {
        if (parse_int(value) != config_format) {
            return std::format("format {} is not supported (this program reads format {})", value, config_format);
        }
    } else if (key == "game_folder") {
        config.game_folder = to_path(value);
    } else if (key == "mt32_rom") {
        config.mt32_rom = to_path(value);
    } else if (key == "mode") {
        if (value != "hd" && value != "original") {
            return "mode is hd or original";
        }
        config.mode = value == "hd" ? GraphicsMode::Hd : GraphicsMode::Original;
    } else if (key == "scale") {
        const auto scale = value == "auto" ? std::optional<int>(0) : parse_int(value);
        if (!scale || *scale < 0 || *scale > max_scale) {
            return std::format("scale is auto or 1 to {}", max_scale);
        }
        config.scale = *scale;
    } else if (key == "music") {
        if (value == "adlib") {
            config.music = MusicDevice::Adlib;
        } else if (value == "mt32") {
            config.music = MusicDevice::Mt32;
        } else if (value == "off") {
            config.music = MusicDevice::Off;
        } else {
            return "music is adlib, mt32 or off";
        }
    } else if (key == "volume") {
        const auto volume = parse_int(value);
        if (!volume || *volume < 0 || *volume > max_volume) {
            return std::format("volume is 0 to {}", max_volume);
        }
        config.volume = *volume;
    } else if (key == "priority_snap") {
        if (value != "on" && value != "off") {
            return "priority_snap is on or off";
        }
        config.priority_snap = value == "on";
    } else {
        parsed.warnings.push_back(std::format("configuration: unknown key {} ignored", key));
    }
    return std::nullopt;
}

} // namespace

core::Result<ParsedConfig> parse_config(std::string_view text) {
    ParsedConfig parsed;
    bool in_section = false;
    bool has_format = false;
    int line_number = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const std::string_view line = trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++line_number;
        const auto fail = [&](std::string_view what) {
            return core::make_error(core::ErrorCode::InvalidData,
                                    std::format("configuration line {}: {}", line_number, what));
        };
        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            in_section = line == "[sierra-hd]";
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) {
            return fail("'key = value' expected");
        }
        if (!in_section) {
            continue;
        }
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view raw = trim(line.substr(equals + 1));
        // Paths may contain ';' and '#'; only other values take comments.
        const bool is_path = key == "game_folder" || key == "mt32_rom";
        const std::string_view value = is_path ? raw : value_of(raw);
        if (auto problem = apply(parsed, key, value)) {
            return fail(*problem);
        }
        has_format = has_format || key == "format";
    }
    if (!has_format) {
        return core::make_error(core::ErrorCode::InvalidData, "configuration: the format line is missing");
    }
    return parsed;
}

namespace {

std::string_view music_name(MusicDevice device) {
    switch (device) {
    case MusicDevice::Adlib:
        return "adlib";
    case MusicDevice::Mt32:
        return "mt32";
    case MusicDevice::Off:
        return "off";
    }
    return "adlib";
}

} // namespace

std::string format_config(const Config& config) {
    const auto path = [](const std::filesystem::path& p) {
        const std::u8string text = p.u8string();
        return text.empty() ? std::string() : " " + std::string(text.begin(), text.end());
    };
    return std::format("; sierra-hd configuration\n"
                       "[sierra-hd]\n"
                       "format = {}\n"
                       "game_folder ={}\n"
                       "; hd or original\n"
                       "mode = {}\n"
                       "; auto (the largest N that fits), or N from 1 to {}\n"
                       "scale = {}\n"
                       "; on: sprites pass behind the picture in whole 1x pixels, as in the original\n"
                       "priority_snap = {}\n"
                       "; folder of the MT-32 ROMs\n"
                       "mt32_rom ={}\n"
                       "; music device: adlib, mt32 (needs the ROMs, otherwise adlib) or off\n"
                       "music = {}\n"
                       "; volume in per cent, 0 to {}\n"
                       "volume = {}\n",
                       config_format, path(config.game_folder), config.mode == GraphicsMode::Hd ? "hd" : "original",
                       max_scale, config.scale == 0 ? std::string("auto") : std::to_string(config.scale),
                       config.priority_snap ? "on" : "off", path(config.mt32_rom), music_name(config.music),
                       max_volume, config.volume);
}

} // namespace sci::platform
