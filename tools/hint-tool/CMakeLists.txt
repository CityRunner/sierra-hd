# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(sci-hint-tool main.cpp)
target_link_libraries(sci-hint-tool PRIVATE sci_tools_common sci-pic sci-vector sci-render)
sci_target_defaults(sci-hint-tool)
