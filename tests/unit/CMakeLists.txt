# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_unit_test(NAME sci-core-tests
    SOURCES
        core/byte_reader_test.cpp
        core/error_test.cpp
        core/fp_profile_test.cpp
        core/log_test.cpp
    DEPENDS sci-core)

sci_add_unit_test(NAME sci-resource-tests
    SOURCES
        resource/decompress_test.cpp
        resource/patch_file_test.cpp
        resource/resource_id_test.cpp
        resource/resource_manager_test.cpp
        resource/resource_map_test.cpp
        resource/volume_test.cpp
    DEPENDS sci-resource)
