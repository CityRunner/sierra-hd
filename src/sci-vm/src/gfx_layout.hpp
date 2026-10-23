// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the comparison files write rectangles and strings: the layout file
// of milestone 6b and the parser file of milestone 6a
// (docs/formats/replay-and-trace.md), as the reference's trace patch writes
// them.

#pragma once

#include "gfx_rect.hpp"

#include <string>
#include <string_view>

namespace sci::vm {

struct Port;

// The text in double quotes, with \", \\ and \xHH for bytes outside 32 to
// 126.
[[nodiscard]] std::string quoted(std::string_view text);
// left,top,right,bottom; offset by the port's origin when a port is given,
// which turns the port's coordinates into screen coordinates.
[[nodiscard]] std::string layout_rect(const Rect& rect, const Port* port = nullptr);

} // namespace sci::vm
