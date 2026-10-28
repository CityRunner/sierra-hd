# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(sci-sound-render main.cpp)
target_link_libraries(sci-sound-render PRIVATE sci_tools_common sci-vm sci-audio sci-platform)
sci_target_defaults(sci-sound-render)
