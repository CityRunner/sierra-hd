// SPDX-License-Identifier: GPL-3.0-or-later

#include "sci/audio/opl3.hpp"

#include "sci/core/assert.hpp"

#include <array>
#include <cstddef>
#include <opl3.h>

namespace sci::audio {

struct Opl3::Chip {
    opl3_chip state;
};

Opl3::Opl3() : chip_(std::make_unique<Chip>()) {
    reset();
}

Opl3::~Opl3() = default;

void Opl3::reset() noexcept {
    // The rate only sets up Nuked's resampler, which generate() does not use.
    OPL3_Reset(&chip_->state, native_rate);
}

void Opl3::write(std::uint16_t reg, std::uint8_t value) noexcept {
    // Nuked marks queued entries with bit 0x200 of the register number.
    SCI_ASSERT(reg < register_count);
    OPL3_WriteRegBuffered(&chip_->state, reg, value);
}

void Opl3::write_now(std::uint16_t reg, std::uint8_t value) noexcept {
    SCI_ASSERT(reg < register_count);
    OPL3_WriteReg(&chip_->state, reg, value);
}

void Opl3::generate(std::span<std::int16_t> frames) noexcept {
    SCI_ASSERT(frames.size() % 2 == 0);
    for (std::size_t i = 0; i < frames.size(); i += 2) {
        std::array<std::int16_t, 2> frame{};
        OPL3_Generate(&chip_->state, frame.data());
        frames[i] = frame[0];
        frames[i + 1] = frame[1];
    }
}

} // namespace sci::audio
