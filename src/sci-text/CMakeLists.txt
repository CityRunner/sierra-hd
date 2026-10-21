# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_library(NAME sci-text
    SOURCES
        src/font.cpp
        src/grammar.cpp
        src/said.cpp
        src/vocabulary.cpp
    DEPENDS sci-core sci-resource)
