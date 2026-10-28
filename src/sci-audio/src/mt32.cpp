// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/audio/mt32.hpp"

#include "sci/core/assert.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <mt32emu/File.h>
#include <mt32emu/ROMInfo.h>
#include <mt32emu/Synth.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sci::audio {

namespace {

// mt32emu's default report handler prints its debug messages and display
// texts to stdout; this one keeps the display text and drops the rest.
class Reports final : public MT32Emu::ReportHandler {
public:
    void printDebug(const char* /*fmt*/, va_list /*list*/) override {}

    void showLCDMessage(const char* message) override { display = message; }

    std::string display;
};

struct RomImageDeleter {
    void operator()(const MT32Emu::ROMImage* image) const noexcept { MT32Emu::ROMImage::freeROMImage(image); }
};

using RomImagePtr = std::unique_ptr<const MT32Emu::ROMImage, RomImageDeleter>;

// One ROM: its bytes, mt32emu's view of them and what it identified.
struct Rom {
    std::vector<std::uint8_t> bytes;
    std::unique_ptr<MT32Emu::ArrayFile> file;
    RomImagePtr image;
};

core::Result<Rom> identify(std::span<const std::uint8_t> bytes, MT32Emu::ROMInfo::Type type, std::string_view what) {
    Rom rom{.bytes = {bytes.begin(), bytes.end()}, .file = {}, .image = {}};
    rom.file = std::make_unique<MT32Emu::ArrayFile>(rom.bytes.data(), rom.bytes.size());
    rom.image.reset(MT32Emu::ROMImage::makeROMImage(rom.file.get()));
    const MT32Emu::ROMInfo* info = rom.image == nullptr ? nullptr : rom.image->getROMInfo();
    if (info == nullptr) {
        return core::make_error(core::ErrorCode::InvalidData,
                                std::format("the {} ({} bytes) is not a ROM mt32emu knows", what, bytes.size()));
    }
    if (info->type != type) {
        return core::make_error(core::ErrorCode::InvalidData,
                                std::format("the {} is the {}, not a {}", what, info->description, what));
    }
    if (info->pairType != MT32Emu::ROMInfo::Full) {
        return core::make_error(
            core::ErrorCode::Unsupported,
            std::format("the {} is one half of a ROM ({}); a whole image is needed", what, info->description));
    }
    return rom;
}

} // namespace

std::optional<Mt32RomInfo> identify_mt32_rom(std::span<const std::uint8_t> rom) {
    // ArrayFile reads the bytes in place; they outlive the image here.
    MT32Emu::ArrayFile file(rom.data(), rom.size());
    const RomImagePtr image(MT32Emu::ROMImage::makeROMImage(&file));
    const MT32Emu::ROMInfo* info = image == nullptr ? nullptr : image->getROMInfo();
    if (info == nullptr || info->pairType != MT32Emu::ROMInfo::Full ||
        (info->type != MT32Emu::ROMInfo::Control && info->type != MT32Emu::ROMInfo::PCM)) {
        return std::nullopt;
    }
    const std::string_view name = info->shortName;
    const bool control = info->type == MT32Emu::ROMInfo::Control;
    const bool cm32l = name.starts_with(control ? "ctrl_cm32l" : "pcm_cm32l");
    return Mt32RomInfo{
        .control = control,
        .model = cm32l ? Mt32Model::Cm32l : Mt32Model::Mt32,
        .name = std::string(name),
        .description = info->description,
    };
}

struct Mt32::State {
    Rom control;
    Rom pcm;
    Mt32Roms roms;
    Reports reports;
    // Constructed with a pointer to `reports`, so declared after it.
    MT32Emu::Synth synth{&reports};
};

core::Result<std::unique_ptr<Mt32>> Mt32::open(std::span<const std::uint8_t> control_rom,
                                               std::span<const std::uint8_t> pcm_rom) {
    auto control = identify(control_rom, MT32Emu::ROMInfo::Control, "control ROM");
    if (!control) {
        return std::unexpected(std::move(control.error()));
    }
    auto pcm = identify(pcm_rom, MT32Emu::ROMInfo::PCM, "PCM ROM");
    if (!pcm) {
        return std::unexpected(std::move(pcm.error()));
    }

    auto state = std::make_unique<State>();
    state->control = std::move(*control);
    state->pcm = std::move(*pcm);
    const MT32Emu::ROMInfo& control_info = *state->control.image->getROMInfo();
    const MT32Emu::ROMInfo& pcm_info = *state->pcm.image->getROMInfo();
    // The CM-32L's control ROMs (and the CM-32LN's) are named ctrl_cm32l*.
    const bool cm32l = std::string_view(control_info.shortName).starts_with("ctrl_cm32l");
    state->roms = Mt32Roms{
        .model = cm32l ? Mt32Model::Cm32l : Mt32Model::Mt32,
        .control_name = control_info.shortName,
        .control_description = control_info.description,
        .pcm_name = pcm_info.shortName,
        .pcm_description = pcm_info.description,
    };

    // The coarse analogue mode outputs at 32000 Hz, the rate the synthesiser
    // runs at internally; the integer renderer is mt32emu's default.
    if (!state->synth.open(*state->control.image, *state->pcm.image, MT32Emu::AnalogOutputMode_COARSE)) {
        return core::make_error(core::ErrorCode::Unsupported,
                                std::format("mt32emu cannot open a synthesiser on the {} with the {}",
                                            control_info.description, pcm_info.description));
    }
    SCI_ASSERT(state->synth.getStereoOutputSampleRate() == native_rate);
    return std::unique_ptr<Mt32>(new Mt32(std::move(state)));
}

Mt32::Mt32(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

// State's members go in reverse order: the synthesiser before the ROM images
// it was opened on.
Mt32::~Mt32() = default;

const Mt32Roms& Mt32::roms() const noexcept {
    return state_->roms;
}

std::uint32_t Mt32::rendered_frames() const noexcept {
    return state_->synth.getInternalRenderedSampleCount();
}

void Mt32::play_now(std::uint32_t message) noexcept {
    state_->synth.playMsgNow(message);
}

void Mt32::play_sysex_now(std::span<const std::uint8_t> sysex) noexcept {
    // mt32emu reads the first byte even of an empty message.
    if (sysex.empty()) {
        return;
    }
    state_->synth.playSysexNow(sysex.data(), static_cast<std::uint32_t>(sysex.size()));
}

void Mt32::write_sysex(std::uint8_t device, std::span<const std::uint8_t> address_and_data) noexcept {
    // mt32emu reads the three address bytes unchecked.
    if (address_and_data.size() < 3) {
        return;
    }
    state_->synth.writeSysex(device, address_and_data.data(), static_cast<std::uint32_t>(address_and_data.size()));
}

bool Mt32::play_at(std::uint32_t message, std::uint32_t timestamp) noexcept {
    return state_->synth.playMsg(message, timestamp);
}

bool Mt32::play_sysex_at(std::span<const std::uint8_t> sysex, std::uint32_t timestamp) noexcept {
    if (sysex.empty()) {
        return true;
    }
    return state_->synth.playSysex(sysex.data(), static_cast<std::uint32_t>(sysex.size()), timestamp);
}

void Mt32::render(std::span<std::int16_t> frames) noexcept {
    SCI_ASSERT(frames.size() % 2 == 0);
    state_->synth.render(frames.data(), static_cast<std::uint32_t>(frames.size() / 2));
}

const std::string& Mt32::display_message() const noexcept {
    return state_->reports.display;
}

} // namespace sci::audio
