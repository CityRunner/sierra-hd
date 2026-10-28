// SPDX-License-Identifier: GPL-3.0-or-later
//
// The save format (milestone 8, docs/formats/save-format.md) on a synthetic
// machine: a save read into a fresh machine gives the same persistent state
// hash, the header comes back as written, and every kind of damage ends in
// a message, never a crash.

#include "save_game.hpp"
#include "save_world.hpp"
#include "sci/core/sha256.hpp"
#include "sci_test.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

using sci::test::SaveWorld;
using sci::test::synthetic_game;

namespace {

constexpr std::size_t checksum_size = 32;

SaveWorld world(std::uint8_t marker = 0) {
    auto game = synthetic_game(marker);
    if (!game) {
        std::abort(); // The synthetic folder always opens.
    }
    return SaveWorld(std::move(*game));
}

std::vector<std::uint8_t> saved(const std::string& description = "In the attic") {
    SaveWorld source = world();
    source.populate();
    return sci::vm::write_save(*source.machine, description, "test 1.0");
}

// Replaces the checksum with the right one, so the damage reaches the state.
void reseal(std::vector<std::uint8_t>& file) {
    file.resize(file.size() - checksum_size);
    const sci::core::Sha256Digest checksum = sci::core::sha256(file);
    file.insert(file.end(), checksum.begin(), checksum.end());
}

bool contains(const std::string& text, const std::string& part) {
    return text.contains(part);
}

// Reads `file` into a fresh machine: the error's message, or "" on success.
std::string restore_error(std::span<const std::uint8_t> file) {
    SaveWorld target = world();
    const auto restored = sci::vm::read_save(*target.machine, file);
    return restored ? std::string() : restored.error().message;
}

} // namespace

SCI_TEST(a_save_restores_the_same_persistent_state) {
    SaveWorld source = world();
    source.populate();
    const sci::vm::StateHash before = sci::vm::state_hash(*source.machine);
    const std::vector<std::uint8_t> file = sci::vm::write_save(*source.machine, "x", "test");
    SCI_CHECK(sci::vm::state_hash(*source.machine).total == before.total); // Saving changes nothing.

    SaveWorld target = world();
    SCI_CHECK(sci::vm::state_hash(*target.machine).persistent != before.persistent);
    const auto restored = sci::vm::read_save(*target.machine, file);
    SCI_CHECK(restored.has_value());
    const sci::vm::StateHash after = sci::vm::state_hash(*target.machine);
    SCI_CHECK(after.persistent == before.persistent);
    for (std::size_t i = 1; i < before.components.size(); ++i) {
        SCI_CHECK(after.components[i].digest == before.components[i].digest);
    }
}

SCI_TEST(the_header_comes_back) {
    SaveWorld source = world();
    const std::vector<std::uint8_t> file = sci::vm::write_save(*source.machine, "Under the stairs", "sierra-hd 0.1.0");
    const sci::core::Sha256Digest game = sci::vm::game_identity(source.resources);
    const auto header = sci::vm::read_save_header(file, &game);
    SCI_CHECK(header.has_value());
    if (header) {
        SCI_CHECK(header->version == sci::vm::save_format_version);
        SCI_CHECK(header->game == game);
        SCI_CHECK(header->description == "Under the stairs");
        SCI_CHECK(header->program == "sierra-hd 0.1.0");
        SCI_CHECK(header->ticks == 0);
        // The deterministic clock's day.
        SCI_CHECK(header->saved_at.year == 90 && header->saved_at.month == 0 && header->saved_at.day == 1);
        SCI_CHECK(header->saved_at.hour == 12 && header->saved_at.minute == 0 && header->saved_at.second == 0);
    }
    SCI_CHECK(std::string(file.begin(), file.begin() + 8) == "SCIHDSAV");
}

SCI_TEST(a_file_that_is_not_a_save_is_refused) {
    std::vector<std::uint8_t> file = saved();
    file[0] ^= 1U;
    SCI_CHECK(contains(restore_error(file), "not a sierra-hd saved game"));
    SCI_CHECK(contains(restore_error(std::vector<std::uint8_t>{}), "not a sierra-hd saved game"));
}

SCI_TEST(another_format_version_is_refused_naming_both) {
    std::vector<std::uint8_t> file = saved();
    file[8] = 2;
    const auto header = sci::vm::read_save_header(file, nullptr);
    SCI_CHECK(!header.has_value() && header.error().code == sci::core::ErrorCode::Unsupported);
    SCI_CHECK(contains(restore_error(file), "format version 2; this program reads version 1"));
}

SCI_TEST(a_truncated_save_is_refused) {
    const std::vector<std::uint8_t> file = saved();
    for (const std::size_t size :
         {std::size_t{4}, std::size_t{12}, std::size_t{30}, file.size() / 2, file.size() - 1}) {
        const std::vector<std::uint8_t> part(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(size));
        const std::string error = restore_error(part);
        SCI_CHECK(contains(error, "not a sierra-hd saved game") || contains(error, "truncated") ||
                  contains(error, "damaged"));
    }
}

SCI_TEST(a_damaged_save_fails_its_checksum) {
    std::vector<std::uint8_t> file = saved();
    file[file.size() / 2] ^= 0x40U;
    SCI_CHECK(contains(restore_error(file), "checksum"));
}

SCI_TEST(a_save_of_another_game_is_refused) {
    const std::vector<std::uint8_t> file = saved();
    SaveWorld other = world(1);
    const auto restored = sci::vm::read_save(*other.machine, file);
    SCI_CHECK(!restored.has_value());
    if (!restored) {
        SCI_CHECK(restored.error().code == sci::core::ErrorCode::Unsupported);
        SCI_CHECK(contains(restored.error().message, "another release"));
    }
}

SCI_TEST(inconsistent_content_is_reported) {
    // The state's first component renamed, with a valid checksum.
    std::vector<std::uint8_t> file = saved();
    const std::string name = "execution";
    const auto found = std::ranges::search(file, name);
    SCI_CHECK(!found.empty());
    *found.begin() = 'E';
    reseal(file);
    const std::string error = restore_error(file);
    SCI_CHECK(contains(error, "cannot be restored") && contains(error, "execution"));
}

SCI_TEST(every_truncated_state_is_reported) {
    // The state cut short by every length (in steps), its length field and
    // the checksum made right: each is an error naming the state.
    const std::vector<std::uint8_t> file = saved();
    // Magic, version, game, description, date, ticks, program.
    constexpr std::size_t length_at = 8 + 4 + 32 + (4 + 12) + 7 + 4 + (4 + 8);
    sci::vm::StateReader length_field(std::span(file).subspan(length_at, 4));
    const std::uint32_t length = length_field.u32();
    SCI_CHECK(length_at + 4 + length + checksum_size == file.size());
    for (std::uint32_t cut = 1; cut <= length; cut += cut < 200 ? 1 : 997) {
        std::vector<std::uint8_t> damaged(file.begin(), file.end() - static_cast<std::ptrdiff_t>(checksum_size + cut));
        const std::uint32_t shorter = length - cut;
        for (std::size_t i = 0; i < 4; ++i) {
            damaged[length_at + i] = static_cast<std::uint8_t>(shorter >> (8 * i));
        }
        damaged.resize(damaged.size() + checksum_size);
        reseal(damaged);
        SCI_CHECK(contains(restore_error(damaged), "cannot be restored"));
    }
}

SCI_TEST(trailing_bytes_are_refused) {
    std::vector<std::uint8_t> file = saved();
    file.insert(file.end() - static_cast<std::ptrdiff_t>(checksum_size), 0);
    reseal(file);
    SCI_CHECK(contains(restore_error(file), "does not fit"));
}

SCI_TEST(write_the_fuzz_seeds_when_asked) {
    // SCI_WRITE_SAVE_SEEDS=<folder>: the seeds of fuzz-save (tests/fuzz/
    // fuzz_save.cpp): mode byte 0 (the file as is) and 1 (the file without
    // its checksum, which the target appends).
    const char* folder = std::getenv("SCI_WRITE_SAVE_SEEDS"); // NOLINT(concurrency-mt-unsafe): tests run alone.
    if (folder == nullptr) {
        return;
    }
    const std::vector<std::uint8_t> file = saved();
    std::vector<std::uint8_t> whole{0};
    whole.insert(whole.end(), file.begin(), file.end());
    std::vector<std::uint8_t> open{1};
    open.insert(open.end(), file.begin(), file.end() - static_cast<std::ptrdiff_t>(checksum_size));
    for (const auto& [name, bytes] : {std::pair{"synthetic-file", whole}, std::pair{"synthetic-unsealed", open}}) {
        std::ofstream out(std::string(folder) + "/" + name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                  static_cast<std::streamsize>(bytes.size()));
        SCI_CHECK(static_cast<bool>(out));
    }
}
