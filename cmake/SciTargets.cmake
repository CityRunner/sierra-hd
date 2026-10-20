# Target helpers that enforce the dependency rules of spec section 4.2.
#
# sci_add_library(NAME <name> [SOURCES ...] [DEPENDS ...])
#   Declares one project library. Libraries may depend only on project
#   libraries declared before them, so the graph is acyclic by construction,
#   and DEPENDS accepts project libraries only. Public headers live in
#   <dir>/include, the implementation in <dir>/src, which other libraries
#   cannot include. External dependencies (SDL3, at milestone 3a) will get a
#   separate helper that admits them for app and sci-audio only.
#
# sci_target_defaults(<target>)
#   Applies the language level and warning set to a project target.

set_property(GLOBAL PROPERTY SCI_DECLARED_LIBRARIES "")

function(sci_target_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    set(warnings
        -Wpedantic
        -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
        -Wcast-qual -Wnon-virtual-dtor -Woverloaded-virtual
        -Wformat=2 -Wimplicit-fallthrough -Wundef -Wnull-dereference
        -Wdouble-promotion -Wextra-semi -Wnewline-eof)
    if(SCI_CLANG_CL)
        # clang-cl reads -Wall as /Wall, which means -Weverything; /W4 is its
        # spelling of -Wall -Wextra. The warnings above are accepted as they are.
        list(PREPEND warnings /W4)
        if(SCI_WARNINGS_AS_ERRORS)
            list(APPEND warnings /WX)
        endif()
    else()
        list(PREPEND warnings -Wall -Wextra)
        if(SCI_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif()
    endif()
    target_compile_options(${target} PRIVATE ${warnings})
endfunction()

function(sci_add_library)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME" "SOURCES;DEPENDS")

    get_property(declared GLOBAL PROPERTY SCI_DECLARED_LIBRARIES)
    foreach(dep IN LISTS arg_DEPENDS)
        if(NOT dep IN_LIST declared)
            message(FATAL_ERROR
                "${arg_NAME} depends on ${dep}, which is not a project library "
                "declared before it (dependencies must be acyclic, spec 4.2).")
        endif()
    endforeach()

    add_library(${arg_NAME} STATIC ${arg_SOURCES})
    add_library(sci::${arg_NAME} ALIAS ${arg_NAME})
    target_include_directories(${arg_NAME}
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${arg_NAME} PUBLIC ${arg_DEPENDS})
    sci_target_defaults(${arg_NAME})

    set_property(GLOBAL APPEND PROPERTY SCI_DECLARED_LIBRARIES ${arg_NAME})
endfunction()
