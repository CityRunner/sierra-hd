# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(sci-hd-frames main.cpp)
target_link_libraries(sci-hd-frames PRIVATE sci_tools_common sci-vm sci-render sci-platform)
sci_target_defaults(sci-hd-frames)
