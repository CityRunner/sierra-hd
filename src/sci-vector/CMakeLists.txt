# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_library(NAME sci-vector
    SOURCES
        src/build_scene.cpp
        src/curves.cpp
        src/fitting.cpp
        src/hints.cpp
        src/topology.cpp
    DEPENDS sci-core sci-pic)
if(SCI_DEBUG_HIGHLIGHT)
    target_compile_definitions(sci-vector PUBLIC SCI_DEBUG_HIGHLIGHT=1)
endif()
