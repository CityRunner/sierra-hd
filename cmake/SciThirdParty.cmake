# SPDX-License-Identifier: GPL-3.0-or-later
#
# Vendored third-party components (spec section 2), built as static
# libraries. Only targets that may use them link them (spec section 4.2):
# SDL3 is for app (window, input, presentation) and, from milestone 7, the
# audio output of sci-audio; Nuked OPL3 and mt32emu are for sci-audio.
#
# Vendored code is not held to the project's warning set (sci_target_defaults
# is not applied), so its warnings never fail the build; the global
# floating-point and sanitizer flags of SciCompilerFlags.cmake still apply.

# --- Nuked OPL3 (milestone 7) ------------------------------------------------
# Built on every platform, including the ARM64 test build without app. Plain
# C; the stereo extension stays off (third_party/nuked-opl3/VENDORED.md), so
# the core has no floating point and no global state.
enable_language(C)
if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
        "Vendored C code is built with upstream LLVM Clang too (spec section 2); "
        "found ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}.")
endif()
add_library(sci_nuked_opl3 STATIC ${PROJECT_SOURCE_DIR}/third_party/nuked-opl3/opl3.c)
target_include_directories(sci_nuked_opl3 SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/nuked-opl3)
target_compile_definitions(sci_nuked_opl3 PUBLIC OPL_ENABLE_STEREOEXT=0)
set_target_properties(sci_nuked_opl3 PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

# --- mt32emu (milestone 7) ---------------------------------------------------
# libmt32emu 2.8.3 from Munt, built on every platform like Nuked OPL3. Only
# the synthesiser core is vendored (third_party/mt32emu/VENDORED.md): no file
# streams, no resamplers, no C interface. Upstream's CMakeLists.txt is not
# used; third_party/mt32emu/config/config.h replaces the config.h it
# generates (static library, C++ API only, no version tagging). The
# synthesiser is reached as <mt32emu/Synth.h> and so on.
set(sci_mt32emu_dir ${PROJECT_SOURCE_DIR}/third_party/mt32emu)
add_library(sci_mt32emu STATIC
    ${sci_mt32emu_dir}/mt32emu/Analog.cpp
    ${sci_mt32emu_dir}/mt32emu/BReverbModel.cpp
    ${sci_mt32emu_dir}/mt32emu/Display.cpp
    ${sci_mt32emu_dir}/mt32emu/File.cpp
    ${sci_mt32emu_dir}/mt32emu/LA32FloatWaveGenerator.cpp
    ${sci_mt32emu_dir}/mt32emu/LA32Ramp.cpp
    ${sci_mt32emu_dir}/mt32emu/LA32WaveGenerator.cpp
    ${sci_mt32emu_dir}/mt32emu/Part.cpp
    ${sci_mt32emu_dir}/mt32emu/Partial.cpp
    ${sci_mt32emu_dir}/mt32emu/PartialManager.cpp
    ${sci_mt32emu_dir}/mt32emu/Poly.cpp
    ${sci_mt32emu_dir}/mt32emu/ROMInfo.cpp
    ${sci_mt32emu_dir}/mt32emu/Synth.cpp
    ${sci_mt32emu_dir}/mt32emu/Tables.cpp
    ${sci_mt32emu_dir}/mt32emu/TVA.cpp
    ${sci_mt32emu_dir}/mt32emu/TVF.cpp
    ${sci_mt32emu_dir}/mt32emu/TVP.cpp
    ${sci_mt32emu_dir}/mt32emu/sha1/sha1.cpp)
target_include_directories(sci_mt32emu SYSTEM PUBLIC ${sci_mt32emu_dir} ${sci_mt32emu_dir}/config)
# Upstream's own build for MSVC-style compilers (clang-cl): the secure
# overloads of the C string functions, instead of deprecation warnings.
target_compile_definitions(sci_mt32emu PRIVATE $<$<BOOL:${SCI_CLANG_CL}>:_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES=1>)
# Upstream code is C++98 style and names no standard; it is built as C++17
# (Clang's default), not the project's C++23. Its warnings are silenced:
# they are not ours to fix and would only bury the project's own.
set_target_properties(sci_mt32emu PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(sci_mt32emu PRIVATE $<IF:$<BOOL:${SCI_CLANG_CL}>,/w,-w>)

# --- SDL3 (milestone 3a) -----------------------------------------------------
# Not in the ARM64 test build (SCI_BUILD_APP=OFF): its sysroot has no ARM64
# window-system libraries.
if(SCI_BUILD_APP)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/SDL3 ${PROJECT_BINARY_DIR}/third_party/SDL3 EXCLUDE_FROM_ALL SYSTEM)
endif()
