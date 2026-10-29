// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project's save format (docs/formats/save-format.md). ScummVM's
// engines/sci/engine/savegame.cpp checks a save's format version and the
// game it was made with (script 0's size and the game object's offset)
// before restoring; this does the same with a version, a hash of the
// game's scripts and vocabularies and a checksum over the whole file.

#include "save_game.hpp"

#include "machine.hpp"
#include "sci/resource/resource_manager.hpp"
#include "state.hpp"
#include "subsystems.hpp"
#include "timing.hpp"

#include <algorithm>
#include <format>

namespace sci::vm {

namespace {

constexpr std::size_t digest_size = 32;

void write_digest(StateWriter& out, const core::Sha256Digest& digest) {
    for (const std::uint8_t byte : digest) {
        out.u8(byte);
    }
}

core::Sha256Digest read_digest(StateReader& in) {
    core::Sha256Digest digest{};
    for (std::uint8_t& byte : digest) {
        byte = in.u8();
    }
    return digest;
}

bool identity_resource(resource::ResourceType type) {
    return type == resource::ResourceType::Script || type == resource::ResourceType::Vocab;
}

} // namespace

core::Sha256Digest game_identity(const resource::ResourceManager& resources) {
    std::vector<resource::ResourceId> ids = resources.list();
    std::erase_if(ids, [](const resource::ResourceId& id) { return !identity_resource(id.type); });
    std::ranges::stable_sort(ids, [](const resource::ResourceId& a, const resource::ResourceId& b) {
        return std::pair(static_cast<int>(a.type), a.number) < std::pair(static_cast<int>(b.type), b.number);
    });
    core::Sha256 hash;
    for (const resource::ResourceId& id : ids) {
        const auto loaded = resources.load(id);
        StateWriter entry;
        entry.u8(static_cast<std::uint8_t>(id.type));
        entry.u16(id.number);
        if (loaded) {
            entry.bytes(loaded->data);
        } else {
            entry.u32(0xFFFFFFFFU); // Unreadable: still part of the identity.
        }
        hash.update(entry.data());
    }
    return hash.finish();
}

std::vector<std::uint8_t> write_save(Machine& machine, std::string_view description, std::string_view program) {
    const Clock& clock = machine.subsystems().clock();
    const LocalTime now = clock.local_time();
    StateWriter state;
    write_state(machine, state);

    StateWriter out;
    for (const char c : save_magic) {
        out.u8(static_cast<std::uint8_t>(c));
    }
    out.u32(save_format_version);
    write_digest(out, game_identity(machine.resources().manager()));
    out.string(description);
    out.u16(static_cast<std::uint16_t>(now.year + 1900));
    out.u8(static_cast<std::uint8_t>(now.month + 1));
    out.u8(static_cast<std::uint8_t>(now.day));
    out.u8(static_cast<std::uint8_t>(now.hour));
    out.u8(static_cast<std::uint8_t>(now.minute));
    out.u8(static_cast<std::uint8_t>(now.second));
    out.u32(clock.play_ticks());
    out.string(program);
    out.bytes(state.data());
    std::vector<std::uint8_t> file = out.data();
    const core::Sha256Digest checksum = core::sha256(file);
    file.insert(file.end(), checksum.begin(), checksum.end());
    return file;
}

namespace {

// The header's fields; `in` is left at the state.
core::Result<SaveHeader> parse_header(std::span<const std::uint8_t> file, const core::Sha256Digest* game,
                                      StateReader& in) {
    constexpr std::size_t prefix = save_magic.size() + 4;
    if (file.size() < prefix ||
        !std::ranges::equal(file.first(save_magic.size()), save_magic,
                            [](std::uint8_t byte, char c) { return byte == static_cast<std::uint8_t>(c); })) {
        return core::make_error(core::ErrorCode::InvalidData, "This is not a sierra-hd saved game.");
    }
    SaveHeader header;
    StateReader version(file.subspan(save_magic.size(), 4));
    header.version = version.u32();
    if (header.version != save_format_version) {
        // No older version exists yet; a newer one comes from a newer
        // program (docs/formats/save-format.md, "Versions").
        return core::make_error(core::ErrorCode::Unsupported,
                                std::format("This saved game has format version {}; this program reads version {}.",
                                            header.version, save_format_version));
    }
    if (file.size() < prefix + digest_size) {
        return core::make_error(core::ErrorCode::InvalidData, "This saved game is truncated.");
    }
    const auto body = file.first(file.size() - digest_size);
    const auto stored = file.last(digest_size);
    const core::Sha256Digest checksum = core::sha256(body);
    if (!std::ranges::equal(checksum, stored)) {
        return core::make_error(core::ErrorCode::InvalidData, "This saved game is damaged (its checksum is wrong).");
    }
    for (std::size_t i = 0; i < prefix; ++i) {
        (void)in.u8();
    }
    header.game = read_digest(in);
    header.description = in.string();
    header.saved_at.year = in.u16() - 1900;
    header.saved_at.month = in.u8() - 1;
    header.saved_at.day = in.u8();
    header.saved_at.hour = in.u8();
    header.saved_at.minute = in.u8();
    header.saved_at.second = in.u8();
    header.ticks = in.u32();
    header.program = in.string();
    if (in.failed()) {
        return core::make_error(core::ErrorCode::InvalidData, "This saved game's header is truncated.");
    }
    if (game != nullptr && header.game != *game) {
        return core::make_error(core::ErrorCode::Unsupported,
                                "This saved game was made with another release of the game.");
    }
    return header;
}

} // namespace

std::optional<SaveHeader> read_save_summary(std::span<const std::uint8_t> file) {
    StateReader in(file);
    for (const char c : save_magic) {
        if (in.u8() != static_cast<std::uint8_t>(c)) {
            return std::nullopt;
        }
    }
    SaveHeader header;
    header.version = in.u32();
    header.game = read_digest(in);
    header.description = in.string();
    header.saved_at.year = in.u16() - 1900;
    header.saved_at.month = in.u8() - 1;
    header.saved_at.day = in.u8();
    header.saved_at.hour = in.u8();
    header.saved_at.minute = in.u8();
    header.saved_at.second = in.u8();
    if (in.failed()) {
        return std::nullopt;
    }
    return header;
}

core::Result<SaveHeader> read_save_header(std::span<const std::uint8_t> file, const core::Sha256Digest* game) {
    StateReader in(file);
    return parse_header(file, game, in);
}

core::Result<SaveHeader> read_save(Machine& machine, std::span<const std::uint8_t> file) {
    const core::Sha256Digest game = game_identity(machine.resources().manager());
    // The reader stops before the checksum.
    const auto body = file.size() >= digest_size ? file.first(file.size() - digest_size) : file;
    StateReader in(body);
    auto header = parse_header(file, &game, in);
    if (!header) {
        return header;
    }
    const std::vector<std::uint8_t> state = in.bytes();
    if (in.failed() || !in.at_end()) {
        return core::make_error(core::ErrorCode::InvalidData, "This saved game's state does not fit the file.");
    }
    StateReader state_in(state);
    if (!read_state(machine, state_in)) {
        return core::make_error(core::ErrorCode::InvalidData,
                                std::format("This saved game cannot be restored: {}.", state_in.message()));
    }
    return header;
}

} // namespace sci::vm
