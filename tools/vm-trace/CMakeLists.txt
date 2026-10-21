# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(sci-vm-trace main.cpp)
target_link_libraries(sci-vm-trace PRIVATE sci_tools_common sci-vm)
sci_target_defaults(sci-vm-trace)
