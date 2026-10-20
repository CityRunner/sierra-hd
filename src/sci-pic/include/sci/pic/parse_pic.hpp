// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sci/core/error.hpp"
#include "sci/pic/pic_command.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sci::pic {

// Decodes an SCI0 picture resource into its command stream, up to the
// terminating opcode 0xFF. Truncated data, data without a terminator and
// out-of-range palette writes are InvalidData; SCI1.1 pictures and
// unsupported extended opcodes are Unsupported. The monochrome extended
// opcodes (2-6) produce no commands.
[[nodiscard]] core::Result<std::vector<PicCommand>> parse_pic(std::span<const std::uint8_t> resource);

} // namespace sci::pic
