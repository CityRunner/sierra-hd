// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace sci::resource {

// Name of this library, reported by the program's --version output.
[[nodiscard]] std::string_view component_name() noexcept;

} // namespace sci::resource
