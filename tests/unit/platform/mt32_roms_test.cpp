// SPDX-License-Identifier: GPL-3.0-or-later
//
// Choosing the music device (spec section 8): the MT-32 needs the user's
// ROMs in the configured folder and falls back to the AdLib without them.
// The tests that find real ROMs read SIERRA_HD_MT32_ROMS and pass with a
// note without it (as in CI); the ROMs are never in the repository.

#include "sci/audio/mt32.hpp"
#include "sci/platform/config.hpp"
#include "sci/platform/mt32_roms.hpp"
#include "sci_test.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <system_error>
#include <vector>

using sci::platform::Config;
using sci::platform::MusicDevice;

namespace {

namespace fs = std::filesystem;

// A new empty folder in the temporary directory, removed at the end.
class TempFolder {
public:
    explicit TempFolder(const std::string& name) : path_(fs::temp_directory_path() / name) {
        std::error_code error;
        fs::remove_all(path_, error);
        fs::create_directories(path_);
    }

    ~TempFolder() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;
    TempFolder(TempFolder&&) = delete;
    TempFolder& operator=(TempFolder&&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

Config mt32_config(const fs::path& folder) {
    Config config;
    config.music = MusicDevice::Mt32;
    config.mt32_rom = folder;
    return config;
}

} // namespace

SCI_TEST(music_choice_keeps_adlib_and_off) {
    Config config;
    config.mt32_rom = "/nowhere";
    auto adlib = sci::platform::choose_music_device(config);
    SCI_CHECK(adlib.device == MusicDevice::Adlib && adlib.mt32 == nullptr && adlib.log.empty());
    config.music = MusicDevice::Off;
    auto off = sci::platform::choose_music_device(config);
    SCI_CHECK(off.device == MusicDevice::Off && off.log.empty());
}

SCI_TEST(music_choice_falls_back_without_a_rom_folder) {
    for (const fs::path& folder : {fs::path(), fs::path("/nowhere/sierra-hd-roms")}) {
        auto choice = sci::platform::choose_music_device(mt32_config(folder));
        SCI_CHECK(choice.device == MusicDevice::Adlib && choice.mt32 == nullptr);
        SCI_CHECK(choice.warning && choice.log.contains("playing on the AdLib"));
    }
}

SCI_TEST(music_choice_falls_back_on_wrong_roms) {
    const TempFolder folder("sierra-hd-mt32-roms-test");
    // Files under the usual names that are no ROMs mt32emu knows, a file
    // that is too large and one without the extension are all passed over.
    std::ofstream(folder.path() / "MT32_CONTROL.ROM", std::ios::binary) << std::string(65536, 'x');
    std::ofstream(folder.path() / "MT32_PCM.rom", std::ios::binary) << std::string(524288, 'y');
    std::ofstream(folder.path() / "big.rom", std::ios::binary) << std::string((1U << 20U) + 1, 'z');
    std::ofstream(folder.path() / "readme.txt") << "not a ROM\n";
    auto choice = sci::platform::choose_music_device(mt32_config(folder.path()));
    SCI_CHECK(choice.device == MusicDevice::Adlib && choice.mt32 == nullptr && choice.warning);
    SCI_CHECK(choice.log.contains("no MT-32 or CM-32L control ROM"));
}

SCI_TEST(mt32_control_ranks_prefer_the_mt32_1_07) {
    SCI_CHECK(sci::platform::mt32_control_rank("ctrl_mt32_1_07") == 0);
    SCI_CHECK(sci::platform::mt32_control_rank("ctrl_mt32_2_04") == 1);
    SCI_CHECK(sci::platform::mt32_control_rank("ctrl_mt32_bluer") == 1);
    SCI_CHECK(sci::platform::mt32_control_rank("ctrl_cm32l_1_02") == 2);
    SCI_CHECK(sci::platform::mt32_control_rank("ctrl_cm32ln_1_00") == 2);
}

SCI_TEST(music_choice_opens_the_users_mt32) {
    const char* roms = std::getenv("SIERRA_HD_MT32_ROMS"); // NOLINT(concurrency-mt-unsafe): no threads here
    if (roms == nullptr || *roms == '\0') {
        std::println("  skipped: SIERRA_HD_MT32_ROMS is not set");
        return;
    }
    // The customer's folder holds the MT-32 1.07, a patched MT-32 control
    // ROM mt32emu does not know and the CM-32L set: the 1.07 is chosen.
    auto choice = sci::platform::choose_music_device(mt32_config(roms));
    SCI_CHECK(choice.device == MusicDevice::Mt32 && choice.mt32 != nullptr && !choice.warning);
    if (choice.mt32 == nullptr) {
        return;
    }
    SCI_CHECK(choice.mt32->roms().control_name == "ctrl_mt32_1_07");
    SCI_CHECK(choice.mt32->roms().pcm_name == "pcm_mt32");
    std::println("  {}", choice.log);

    // With only the CM-32L's ROMs, the CM-32L.
    const TempFolder cm32l("sierra-hd-mt32-roms-cm32l");
    std::error_code error;
    for (const char* name : {"CM32L_CONTROL.1989-12-05.v1.02.ROM", "CM32L_PCM.ROM"}) {
        fs::copy_file(fs::path(roms) / name, cm32l.path() / name, error);
    }
    if (error) {
        std::println("  skipped: no CM-32L ROMs in {}", roms);
        return;
    }
    auto second = sci::platform::choose_music_device(mt32_config(cm32l.path()));
    SCI_CHECK(second.device == MusicDevice::Mt32 && second.mt32 != nullptr);
    SCI_CHECK(second.mt32 && second.mt32->roms().model == sci::audio::Mt32Model::Cm32l);

    // A control ROM without its PCM ROM falls back.
    fs::remove(cm32l.path() / "CM32L_PCM.ROM", error);
    auto third = sci::platform::choose_music_device(mt32_config(cm32l.path()));
    SCI_CHECK(third.device == MusicDevice::Adlib && third.warning && third.log.contains("no PCM ROM"));
}
