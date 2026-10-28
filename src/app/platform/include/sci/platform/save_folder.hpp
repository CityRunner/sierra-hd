// SPDX-License-Identifier: GPL-3.0-or-later
//
// The saved games in a folder (docs/formats/save-format.md, "Slots and the
// save folder"): slot n is the file `sierra-hd-<nnn>.sav`, as ScummVM names
// a game's saves `<target>.<nnn>`. The program keeps them in `saves` next to
// its configuration; sci-vm-trace in a folder of its own per run.
#pragma once

#include "sci/vm/files.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sci::platform {

class SaveFolder final : public vm::SaveStore {
public:
    // The folder is created when the first save is written.
    explicit SaveFolder(std::filesystem::path folder) : folder_(std::move(folder)) {}

    [[nodiscard]] const std::filesystem::path& folder() const { return folder_; }

    // The file name of a slot: sierra-hd-001.sav for slot 1.
    [[nodiscard]] static std::string file_name(int slot);

    [[nodiscard]] std::vector<int> slots() override;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read(int slot) override;
    bool write(int slot, std::span<const std::uint8_t> data) override;

private:
    std::filesystem::path folder_;
};

} // namespace sci::platform
