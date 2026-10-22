// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ported from ScummVM (https://www.scummvm.org) at revision
// ed949213a23440447ea402e263daa871394b6bf9, from:
//   common/str.cpp (matchString), engines/sci/engine/kfile.cpp (kDeviceInfo,
//   K_DEVICE_INFO_PATHS_EQUAL)
// and translated into this project's style, types and error handling.
//
// ScummVM - Graphic Adventure Engine
//
// ScummVM is the legal property of its developers, whose names are too
// numerous to list here. Please refer to the COPYRIGHT file distributed with
// this source distribution.
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <string_view>

namespace sci::vm {

// Common::matchString(str, pattern, false, "/"): * any run, ? one
// character, # one digit, \\ escapes; neither wildcard crosses a slash.
// The end of the string counts as excluded too, as strchr finds the NUL.
[[nodiscard]] bool match_path(std::string_view str, std::string_view pattern);

} // namespace sci::vm
