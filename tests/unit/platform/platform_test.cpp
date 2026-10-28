// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/config.hpp"
#include "sci/platform/identify.hpp"
#include "sci_test.hpp"

#include <array>
#include <cstdint>
#include <vector>

using sci::platform::GraphicsMode;
using sci::platform::MusicDevice;
using sci::platform::parse_config;

SCI_TEST(config_reads_its_keys) {
    const auto parsed = parse_config("; comment\n[sierra-hd]\nformat = 1\ngame_folder = C:\\Games\\Laura;1\n"
                                     "mode = original ; comment\nscale = 5\nmt32_rom =\ncolour = red\n");
    SCI_CHECK(parsed.has_value());
    if (!parsed) {
        return;
    }
    SCI_CHECK(parsed->config.game_folder.u8string() == u8"C:\\Games\\Laura;1");
    SCI_CHECK(parsed->config.mode == GraphicsMode::Original);
    SCI_CHECK(parsed->config.scale == 5);
    SCI_CHECK(parsed->config.mt32_rom.empty());
    SCI_CHECK(!parsed->config.priority_snap); // Off by default.
    SCI_CHECK(parsed->warnings.size() == 1);
}

SCI_TEST(config_reads_priority_snap) {
    const auto on = parse_config("[sierra-hd]\nformat = 1\npriority_snap = on\n");
    SCI_CHECK(on.has_value() && on->config.priority_snap);
    const auto off = parse_config("[sierra-hd]\nformat = 1\npriority_snap = off ; comment\n");
    SCI_CHECK(off.has_value() && !off->config.priority_snap);
    SCI_CHECK(!parse_config("[sierra-hd]\nformat = 1\npriority_snap = yes\n").has_value());
}

SCI_TEST(config_reads_music_and_volume) {
    const auto defaults = parse_config("[sierra-hd]\nformat = 1\n");
    SCI_CHECK(defaults.has_value() && defaults->config.music == MusicDevice::Adlib && defaults->config.volume == 100);
    const auto set = parse_config("[sierra-hd]\nformat = 1\nmusic = off ; silent\nvolume = 40\n");
    SCI_CHECK(set.has_value() && set->config.music == MusicDevice::Off && set->config.volume == 40);
    const auto mt32 = parse_config("[sierra-hd]\nformat = 1\nmusic = mt32\n");
    SCI_CHECK(mt32.has_value() && mt32->config.music == MusicDevice::Mt32);
    SCI_CHECK(!parse_config("[sierra-hd]\nformat = 1\nmusic = gm\n").has_value());
    SCI_CHECK(!parse_config("[sierra-hd]\nformat = 1\nvolume = 101\n").has_value());
    SCI_CHECK(!parse_config("[sierra-hd]\nformat = 1\nvolume = -1\n").has_value());
}

SCI_TEST(config_round_trips) {
    sci::platform::Config config;
    config.game_folder = "/home/player/laura bow";
    config.scale = 0;
    config.priority_snap = true;
    config.music = MusicDevice::Off;
    config.volume = 75;
    const auto parsed = parse_config(sci::platform::format_config(config));
    SCI_CHECK(parsed.has_value() && parsed->config.game_folder == config.game_folder && parsed->config.scale == 0 &&
              parsed->config.mode == GraphicsMode::Hd && parsed->config.priority_snap && parsed->warnings.empty());
    SCI_CHECK(parsed.has_value() && parsed->config.music == MusicDevice::Off && parsed->config.volume == 75);
}

SCI_TEST(config_rejects_what_it_cannot_read) {
    SCI_CHECK(!parse_config("[sierra-hd]\nmode = hd\n").has_value());  // No format.
    SCI_CHECK(!parse_config("[sierra-hd]\nformat = 2\n").has_value()); // Newer format.
    SCI_CHECK(!parse_config("[sierra-hd]\nformat = 1\nscale = 17\n").has_value());
    SCI_CHECK(!parse_config("[sierra-hd]\nformat = 1\nmode = vga\n").has_value());
    SCI_CHECK(!parse_config("[sierra-hd]\nformat = 1\nnonsense\n").has_value());
}

SCI_TEST(unknown_game_folders_are_rejected) {
    const std::vector<sci::resource::GameFile> none;
    SCI_CHECK(!sci::platform::identify_game(none).has_value());
    const std::vector<sci::resource::GameFile> other{
        {.name = "resource.map", .bytes = std::vector<std::uint8_t>(8478, 0)},
    };
    const auto id = sci::platform::identify_game(other);
    SCI_CHECK(!id.has_value() && id.error().code == sci::core::ErrorCode::Unsupported);
}
