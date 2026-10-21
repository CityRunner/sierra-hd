# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_unit_test(NAME sci-core-tests
    SOURCES
        core/byte_reader_test.cpp
        core/error_test.cpp
        core/exact_test.cpp
        core/fp_profile_test.cpp
        core/json_test.cpp
        core/log_test.cpp
        core/sha256_test.cpp
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

sci_add_unit_test(NAME sci-pic-tests
    SOURCES
        pic/draw_pic_test.cpp
        pic/parse_pic_test.cpp
    DEPENDS sci-pic)

sci_add_unit_test(NAME sci-view-tests
    SOURCES view/view_test.cpp
    DEPENDS sci-view)

sci_add_unit_test(NAME sci-text-tests
    SOURCES text/font_test.cpp
    DEPENDS sci-text)

sci_add_unit_test(NAME sci-vector-tests
    SOURCES
        vector/build_scene_test.cpp
        vector/differential_test.cpp
        vector/fitting_test.cpp
        vector/hint_scene_test.cpp
        vector/hints_test.cpp
    DEPENDS sci-vector sci-render)
# The fitting tests reach into sci-vector's internals.
target_include_directories(sci-vector-tests PRIVATE pic ${PROJECT_SOURCE_DIR}/src/sci-vector/src)

sci_add_unit_test(NAME sci-render-tests
    SOURCES render/render_test.cpp
    DEPENDS sci-render)
