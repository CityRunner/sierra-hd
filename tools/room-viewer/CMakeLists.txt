# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(sci-room-viewer main.cpp)
target_link_libraries(sci-room-viewer PRIVATE sci_tools_common sci-pic sci-text sci-view)
sci_target_defaults(sci-room-viewer)
