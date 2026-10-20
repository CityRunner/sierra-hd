# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(sci-graphics-dump main.cpp)
target_link_libraries(sci-graphics-dump PRIVATE sci_tools_common sci-pic sci-text sci-view)
sci_target_defaults(sci-graphics-dump)
