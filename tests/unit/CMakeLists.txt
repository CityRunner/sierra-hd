# SPDX-License-Identifier: GPL-3.0-or-later
sci_add_unit_test(NAME sci-core-tests
    SOURCES
        core/byte_reader_test.cpp
        core/error_test.cpp
        core/exact_test.cpp
        core/fp_profile_test.cpp
        core/json_test.cpp
        core/log_test.cpp
        core/md5_test.cpp
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
    SOURCES view/cursor_test.cpp view/view_test.cpp
    DEPENDS sci-view)

sci_add_unit_test(NAME sci-text-tests
    SOURCES text/font_test.cpp text/parser_test.cpp
    DEPENDS sci-text)

sci_add_unit_test(NAME sci-vector-tests
    SOURCES
        vector/build_scene_test.cpp
        vector/differential_test.cpp
        vector/fitting_test.cpp
        vector/hint_scene_test.cpp
        vector/hints_test.cpp
        vector/scene_io_test.cpp
        vector/stroke_integrity_test.cpp
    DEPENDS sci-vector sci-render)
# The fitting tests reach into sci-vector's internals.
target_include_directories(sci-vector-tests PRIVATE pic ${PROJECT_SOURCE_DIR}/src/sci-vector/src)

sci_add_unit_test(NAME sci-audio-tests
    SOURCES audio/adlib_test.cpp audio/mixer_test.cpp audio/mt32_driver_test.cpp audio/mt32_test.cpp audio/opl3_test.cpp
    DEPENDS sci-audio)

sci_add_unit_test(NAME sci-render-tests
    SOURCES render/compose_test.cpp render/render_test.cpp
    DEPENDS sci-render sci-vector sci-pic)
target_include_directories(sci-render-tests PRIVATE pic render)

# The interface checks of sci-hd-frames are compiled from the tool's
# sources (tools/common).
sci_add_unit_test(NAME sci-platform-tests
    SOURCES
        platform/analysis_cache_test.cpp
        platform/game_frame_test.cpp
        platform/interface_check_test.cpp
        platform/mt32_roms_test.cpp
        platform/platform_test.cpp
        platform/save_folder_test.cpp
        ${PROJECT_SOURCE_DIR}/tools/common/interface_check.cpp
    DEPENDS sci-platform)
target_include_directories(sci-platform-tests PRIVATE pic render ${PROJECT_SOURCE_DIR}/tools/common)

sci_add_unit_test(NAME sci-vm-tests
    SOURCES
        vm/files_test.cpp
        vm/input_test.cpp
        vm/music_device_test.cpp
        vm/music_test.cpp
        vm/path_match_test.cpp
        vm/save_kernel_test.cpp
        vm/save_test.cpp
        vm/screen_test.cpp
        vm/script_patcher_test.cpp
        vm/state_test.cpp
    DEPENDS sci-vm)
# The tests reach into sci-vm's internals; the state tests build a resource
# folder.
target_include_directories(sci-vm-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/sci-vm/src resource)

# The window layer's key and cursor conversion, compiled from the program's
# sources (the program itself is no library).
sci_add_unit_test(NAME sci-app-tests
    SOURCES
        app/game_screen_test.cpp
        ${PROJECT_SOURCE_DIR}/src/app/src/game_screen.cpp
    DEPENDS sci-vm sci-render sci-platform sci-view sci-text sci-resource sci-core)
target_include_directories(sci-app-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app/src)

# The music's hand-off to SDL's audio thread (milestone 7), on SDL's dummy
# audio driver; label "threads" for the TSan job. Not in the ARM64 test
# build, which has no SDL3.
if(SCI_BUILD_APP)
    sci_add_unit_test(NAME sci-audio-output-tests
        SOURCES
            app/audio_output_test.cpp
            ${PROJECT_SOURCE_DIR}/src/app/src/audio_output.cpp
        DEPENDS sci-audio sci-core
        LABELS threads)
    target_include_directories(sci-audio-output-tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app/src)
    target_link_libraries(sci-audio-output-tests PRIVATE SDL3::SDL3-static)
endif()
