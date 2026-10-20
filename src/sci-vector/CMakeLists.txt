# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_library(NAME sci-vector
    SOURCES
        src/build_scene.cpp
        src/curves.cpp
        src/fitting.cpp
        src/topology.cpp
    DEPENDS sci-core sci-pic)
