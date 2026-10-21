// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/platform/identify.hpp"

#include "sci/core/md5.hpp"
#include "sci/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

namespace sci::platform {

namespace {

struct KnownFile {
    std::string_view name;
    std::size_t size;
    std::string_view sha256;
};

// The tested release (docs/ASSUMPTIONS.md). The interpreter files
// (SCIV.EXE, the drivers) are not needed and not checked.
constexpr std::array tested_resources{
    KnownFile{
        .name = "RESOURCE.MAP",
        .size = 8478,
        .sha256 = "47c3463102b15034a0ba7d561c8c1c2de9838f05b8ecbc286961bdea862ba13b",
    },
    KnownFile{
        .name = "RESOURCE.001",
        .size = 515788,
        .sha256 = "898b89ad5ca9474cab37a8bf4fe8f22016700870fcc3eceb6953ae9e39cf4eb8",
    },
    KnownFile{
        .name = "RESOURCE.002",
        .size = 721381,
        .sha256 = "db1fa10ce5e438774fb44aef9c69eaa22ba82a403b05052533b9b5b11863a819",
    },
    KnownFile{
        .name = "RESOURCE.003",
        .size = 667468,
        .sha256 = "e114602aeb7e6b25ba4471bcbc1d15778841b4e4cf0c15d11fc4f58b491551f3",
    },
    KnownFile{
        .name = "RESOURCE.004",
        .size = 683807,
        .sha256 = "fcf2d7252dfd06b8ed8ca701128023fe404ad52c066b7548eec0eda3840cf762",
    },
};

constexpr std::array tested_patches{
    KnownFile{
        .name = "SCRIPT.052",
        .size = 6962,
        .sha256 = "4f8a42dbba46fe3b7156b90fdca8f0c526d3509fed623425983c072fda5254c7",
    },
    KnownFile{
        .name = "TEXT.052",
        .size = 2734,
        .sha256 = "7b8e4af2d9101764dbdd790988b892a0477297578cf02dd9c3d1100a3cc4bd73",
    },
};

// ScummVM's detection entry of the 5.25" release: the MD5 of the first
// 5000 bytes of RESOURCE.MAP, and its size.
constexpr std::string_view floppy525_map_md5 = "b1905f6aa68ff65a057b080b1eae954c";
constexpr std::size_t floppy525_map_size = 12030;
constexpr std::size_t detection_bytes = 5000;

bool same_name(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, [](char x, char y) {
        const auto upper = [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; };
        return upper(x) == upper(y);
    });
}

const resource::GameFile* find(std::span<const resource::GameFile> files, std::string_view name) {
    const auto it = std::ranges::find_if(files, [&](const resource::GameFile& f) { return same_name(f.name, name); });
    return it == files.end() ? nullptr : &*it;
}

bool matches(const resource::GameFile* file, const KnownFile& known) {
    return file != nullptr && file->bytes.size() == known.size &&
           core::to_hex(core::sha256(file->bytes)) == known.sha256;
}

} // namespace

core::Result<Identification> identify_game(std::span<const resource::GameFile> files) {
    const resource::GameFile* map = find(files, "RESOURCE.MAP");
    if (map == nullptr) {
        return core::make_error(core::ErrorCode::NotFound,
                                "There is no RESOURCE.MAP in this folder. Choose the folder of The Colonel's Bequest.");
    }
    if (matches(map, tested_resources[0])) {
        for (const KnownFile& known : std::span(tested_resources).subspan(1)) {
            if (!matches(find(files, known.name), known)) {
                return core::make_error(core::ErrorCode::Unsupported,
                                        std::format("{} is missing or differs from the English DOS release. The game "
                                                    "files may be damaged or modified.",
                                                    known.name));
            }
        }
        Identification id{
            .release = Release::Tested,
            .name = "The Colonel's Bequest, English DOS 3.5\" release 1.000.046",
            .warnings = {},
        };
        for (const KnownFile& known : tested_patches) {
            const resource::GameFile* patch = find(files, known.name);
            if (patch == nullptr) {
                id.warnings.push_back(std::format("The patch file {} is missing; the game may differ from the tested "
                                                  "copy.",
                                                  known.name));
            } else if (!matches(patch, known)) {
                id.warnings.push_back(std::format(
                    "The patch file {} differs from the tested copy; the game may behave differently.", known.name));
            }
        }
        return id;
    }
    const std::span<const std::uint8_t> head(map->bytes.data(), std::min(map->bytes.size(), detection_bytes));
    if (map->bytes.size() == floppy525_map_size && core::to_hex(core::md5(head)) == floppy525_map_md5) {
        return Identification{
            .release = Release::Floppy525,
            .name = "The Colonel's Bequest, English DOS 5.25\" release 1.000.046",
            .warnings =
                {
                    "This release has not been tested with sierra-hd. The HD hints are made for the 3.5\" "
                    "release and are not used.",
                },
        };
    }
    return core::make_error(core::ErrorCode::Unsupported,
                            "This is not a supported release of The Colonel's Bequest. sierra-hd runs the English "
                            "DOS EGA releases (see the README).");
}

} // namespace sci::platform
