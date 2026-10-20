# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_library(NAME sci-resource
    SOURCES
        src/decompress.cpp
        src/patch_file.cpp
        src/resource_id.cpp
        src/resource_manager.cpp
        src/resource_map.cpp
        src/volume.cpp
    DEPENDS sci-core)
