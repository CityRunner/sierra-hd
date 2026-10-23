# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_library(NAME sci-audio
    SOURCES src/component.cpp src/opl3.cpp
    DEPENDS sci-core sci-resource)
# The OPL core (cmake/SciThirdParty.cmake), private: its header is used only
# in src/opl3.cpp.
target_link_libraries(sci-audio PRIVATE sci_nuked_opl3)
