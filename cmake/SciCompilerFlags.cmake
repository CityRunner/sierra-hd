# Compiler flags shared by every target, including vendored code.
#
# The floating-point profile (spec section 5.3) and the sanitizer selection
# must apply to everything linked into one binary, so they are set globally
# here. Warnings are per target (see SciTargets.cmake) because vendored code is
# not held to this project's warning set.

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
        "sierra-hd is built with upstream LLVM Clang only (spec section 2); "
        "found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}.")
endif()

set(SCI_CLANG_CL OFF)
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(SCI_CLANG_CL ON)
endif()

# Passes a GCC-style driver option, spelled the way clang-cl accepts it.
function(sci_driver_option out option)
    if(SCI_CLANG_CL)
        set(${out} "/clang:${option}" PARENT_SCOPE)
    else()
        set(${out} "${option}" PARENT_SCOPE)
    endif()
endfunction()

# --- Floating-point profile (spec section 5.3) -----------------------------
# No contraction into FMA on any platform; clang-cl's default /fp:precise
# enables contraction, so the option is passed explicitly in both drivers.
# SSE2 is the x86-64 baseline, so no x87 code is generated for double; the
# profile is also checked at run time by sci::core::check_fp_profile().
sci_driver_option(fp_contract_off "-ffp-contract=off")
add_compile_options(${fp_contract_off} "$<$<NOT:$<BOOL:${SCI_CLANG_CL}>>:-fno-fast-math>")

# --- Exceptions (docs/ARCHITECTURE.md, "Error handling") -------------------
# The project does not use exceptions; failures are values (std::expected).
if(SCI_CLANG_CL)
    # CMake adds /EHsc to the default flags for MSVC-style compilers.
    string(REGEX REPLACE "/EH[a-z-]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_compile_options(/EHs-c-)
    add_compile_definitions(_HAS_EXCEPTIONS=0)
else()
    add_compile_options(-fno-exceptions)
endif()

# --- Linker -----------------------------------------------------------------
if(NOT SCI_CLANG_CL AND NOT APPLE)
    add_link_options(-fuse-ld=lld)
endif()

# --- Sanitizers and integer overflow (spec section 5.2) --------------------
set(SCI_SANITIZE "" CACHE STRING
    "Sanitizers to enable: empty, 'address;undefined', 'thread' or 'address'")
set(SCI_FUZZ OFF CACHE BOOL "Build the libFuzzer targets (implies ASan and UBSan)")

if(SCI_FUZZ)
    if(SCI_CLANG_CL)
        message(FATAL_ERROR "Fuzzing is run on Linux and macOS only.")
    endif()
    set(SCI_SANITIZE "address;undefined" CACHE STRING "" FORCE)
    add_compile_options(-fsanitize=fuzzer-no-link)
endif()

if(SCI_SANITIZE)
    list(JOIN SCI_SANITIZE "," sanitize_list)
    if(SCI_CLANG_CL)
        if(NOT SCI_SANITIZE STREQUAL "address")
            message(FATAL_ERROR "Only ASan is supported with clang-cl (spec section 5.2).")
        endif()
        add_compile_options(-fsanitize=address /Oy-)
        # CMake runs lld-link directly, so the ASan runtime that clang-cl
        # would add when driving the link is named here, with the same
        # arguments as clang-cl uses for the dynamic CRT (/MD).
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} /clang:-print-resource-dir
                        OUTPUT_VARIABLE clang_resource_dir OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
        # Release archives use either lib/windows/<name>-x86_64.lib or
        # lib/x86_64-pc-windows-msvc/<name>.lib.
        find_file(SCI_ASAN_RUNTIME NAMES clang_rt.asan_dynamic-x86_64.lib clang_rt.asan_dynamic.lib
                  PATHS "${clang_resource_dir}/lib/windows" "${clang_resource_dir}/lib/x86_64-pc-windows-msvc"
                  NO_DEFAULT_PATH REQUIRED)
        find_file(SCI_ASAN_THUNK
                  NAMES clang_rt.asan_dynamic_runtime_thunk-x86_64.lib clang_rt.asan_dynamic_runtime_thunk.lib
                  PATHS "${clang_resource_dir}/lib/windows" "${clang_resource_dir}/lib/x86_64-pc-windows-msvc"
                  NO_DEFAULT_PATH REQUIRED)
        add_link_options("${SCI_ASAN_RUNTIME}" "/include:__asan_seh_interceptor" "/wholearchive:${SCI_ASAN_THUNK}")
    else()
        add_compile_options(-fsanitize=${sanitize_list} -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=${sanitize_list})
    endif()
else()
    # Safety net for release builds only: sanitizer builds omit it so that
    # UBSan reports every signed overflow, which is a bug in this project.
    sci_driver_option(fwrapv "-fwrapv")
    add_compile_options("$<$<CONFIG:Release,RelWithDebInfo>:${fwrapv}>")
endif()

# --- Standard library assertions in debug and sanitizer builds -------------
if(SCI_SANITIZE)
    add_compile_definitions(_GLIBCXX_ASSERTIONS _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
else()
    add_compile_definitions(
        "$<$<CONFIG:Debug>:_GLIBCXX_ASSERTIONS>"
        "$<$<CONFIG:Debug>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE>")
endif()

if(SCI_CLANG_CL)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    add_compile_options(/utf-8 /Zc:__cplusplus)
endif()
