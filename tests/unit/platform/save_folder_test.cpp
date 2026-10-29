// SPDX-License-Identifier: GPL-3.0-or-later
//
// The saved games in a folder (docs/formats/save-format.md, "Slots and the
// save folder"), and the configuration's save folder.

#include "sci/platform/config.hpp"
#include "sci/platform/save_folder.hpp"
#include "sci_test.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using sci::platform::SaveFolder;

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

void write_file(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

} // namespace

SCI_TEST(save_folder_names_slots) {
    SCI_CHECK(SaveFolder::file_name(1) == "sierra-hd-001.sav");
    SCI_CHECK(SaveFolder::file_name(42) == "sierra-hd-042.sav");
    SCI_CHECK(SaveFolder::file_name(99) == "sierra-hd-099.sav");
}

SCI_TEST(save_folder_writes_and_reads_back) {
    const TempFolder temp("sierra-hd-save-folder-write");
    // The folder itself is created by the first write.
    SaveFolder saves(temp.path() / "saves");
    SCI_CHECK(saves.slots().empty());
    const std::vector<std::uint8_t> first{1, 2, 3, 0, 255};
    const std::vector<std::uint8_t> second{7};
    SCI_CHECK(saves.write(3, first));
    SCI_CHECK(saves.write(12, second));
    SCI_CHECK(fs::is_regular_file(temp.path() / "saves" / "sierra-hd-003.sav"));
    SCI_CHECK(saves.slots() == std::vector<int>({3, 12}));
    SCI_CHECK(saves.read(3) == first);
    SCI_CHECK(saves.read(12) == second);
    // Writing a slot again replaces its file.
    SCI_CHECK(saves.write(3, second));
    SCI_CHECK(saves.read(3) == second);
    SCI_CHECK(!saves.read(4).has_value());
}

SCI_TEST(save_folder_lists_slots_in_order) {
    const TempFolder temp("sierra-hd-save-folder-order");
    SaveFolder saves(temp.path());
    for (const int slot : {50, 1, 99, 7}) {
        SCI_CHECK(saves.write(slot, std::vector<std::uint8_t>{static_cast<std::uint8_t>(slot)}));
    }
    SCI_CHECK(saves.slots() == std::vector<int>({1, 7, 50, 99}));
}

SCI_TEST(save_folder_without_its_folder_is_empty) {
    const TempFolder temp("sierra-hd-save-folder-missing");
    SaveFolder saves(temp.path() / "not-there");
    SCI_CHECK(saves.slots().empty());
    SCI_CHECK(!saves.read(1).has_value());
    SCI_CHECK(!fs::exists(temp.path() / "not-there"));
}

SCI_TEST(save_folder_leaves_out_other_files) {
    const TempFolder temp("sierra-hd-save-folder-other");
    for (const char* name : {
             "sierra-hd-000.sav",
             "sierra-hd-100.sav",
             "sierra-hd-1.sav",
             "sierra-hd-0001.sav",
             "sierra-hd-00a.sav",
             "sierra-hd-005.SAV",
             "sierra-hd-005.sav.bak",
             "colonel.005",
             "laura.sav",
             "SIERRA-HD-006.sav",
         }) {
        write_file(temp.path() / name, "x");
    }
    // A folder with a slot's name is not a save.
    fs::create_directories(temp.path() / "sierra-hd-008.sav");
    write_file(temp.path() / "sierra-hd-002.sav", "not a save, but in a slot");
    SaveFolder saves(temp.path());
    // The folder lists every file named as a slot; whether it is a save is
    // decided from its contents (kGetSaveFiles leaves it out).
    SCI_CHECK(saves.slots() == std::vector<int>({2}));
    const auto bytes = saves.read(2);
    SCI_CHECK(bytes.has_value() && bytes->size() == 25);
    SCI_CHECK(!saves.read(8).has_value());
}

SCI_TEST(save_folder_write_fails_where_it_cannot_write) {
    const TempFolder temp("sierra-hd-save-folder-fail");
    // A file where the folder should be.
    write_file(temp.path() / "file", "x");
    SaveFolder saves(temp.path() / "file");
    SCI_CHECK(!saves.write(1, std::vector<std::uint8_t>{1}));
    SCI_CHECK(saves.slots().empty());
}

SCI_TEST(config_names_the_save_folder) {
    const fs::path base = fs::path("base") / "folder";
    sci::platform::Config config;
    SCI_CHECK(sci::platform::saves_folder(config, base) == base / "saves");
    config.saves = "my saves";
    SCI_CHECK(sci::platform::saves_folder(config, base) == base / "my saves");
    const fs::path absolute = fs::temp_directory_path() / "laura";
    config.saves = absolute;
    SCI_CHECK(sci::platform::saves_folder(config, base) == absolute);

    const auto parsed = sci::platform::parse_config("[sierra-hd]\nformat = 1\nsaves = /games/laura;saves#1\n");
    SCI_CHECK(parsed.has_value() && parsed->config.saves.u8string() == u8"/games/laura;saves#1" &&
              parsed->warnings.empty());
    const auto defaults = sci::platform::parse_config("[sierra-hd]\nformat = 1\n");
    SCI_CHECK(defaults.has_value() && defaults->config.saves.empty());
    const auto round_trip = sci::platform::parse_config(sci::platform::format_config(parsed->config));
    SCI_CHECK(round_trip.has_value() && round_trip->config.saves == parsed->config.saves);
}
