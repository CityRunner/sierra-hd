# SPDX-License-Identifier: GPL-3.0-or-later
add_subdirectory(platform)

add_executable(sierra-hd src/main.cpp src/viewer.cpp)
target_link_libraries(sierra-hd PRIVATE
    sci-core sci-resource sci-pic sci-vector sci-view sci-text sci-audio sci-render sci-vm sci-platform
    SDL3::SDL3-static)
target_compile_definitions(sierra-hd PRIVATE SCI_VERSION="${PROJECT_VERSION}")
sci_target_defaults(sierra-hd)

# The Linux binary carries its C++ runtime, so it runs on distributions
# with an older libstdc++ than the toolchain image (sanitizer builds keep
# the shared runtime the sanitizers expect).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT SCI_SANITIZE)
    target_link_options(sierra-hd PRIVATE -static-libstdc++ -static-libgcc)
endif()
