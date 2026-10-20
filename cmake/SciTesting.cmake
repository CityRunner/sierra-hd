# Test helpers.
#
# sci_add_unit_test(NAME <name> SOURCES ... DEPENDS ...)
#   One test executable built on tests/support, registered with CTest.
#
# sci_add_fuzz_target(NAME <name> SOURCE <file> DEPENDS ... CORPUS <dir>)
#   With SCI_FUZZ=ON: a libFuzzer binary, run by scripts/run-fuzzers.sh.
#   Otherwise: the same entry point linked to a small driver that replays the
#   seed corpus, registered with CTest, so every platform runs the inputs.

function(sci_add_unit_test)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME" "SOURCES;DEPENDS")
    add_executable(${arg_NAME} ${arg_SOURCES})
    target_link_libraries(${arg_NAME} PRIVATE sci_test_support ${arg_DEPENDS})
    sci_target_defaults(${arg_NAME})
    add_test(NAME ${arg_NAME} COMMAND ${arg_NAME})
endfunction()

function(sci_add_fuzz_target)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;SOURCE;CORPUS" "DEPENDS")
    add_executable(${arg_NAME} ${arg_SOURCE})
    target_link_libraries(${arg_NAME} PRIVATE ${arg_DEPENDS})
    sci_target_defaults(${arg_NAME})
    if(SCI_FUZZ)
        target_link_options(${arg_NAME} PRIVATE -fsanitize=fuzzer)
        set_property(GLOBAL APPEND PROPERTY SCI_FUZZ_TARGETS "${arg_NAME}=${arg_CORPUS}")
    else()
        target_link_libraries(${arg_NAME} PRIVATE sci_fuzz_replay_main)
        add_test(NAME ${arg_NAME}-corpus COMMAND ${arg_NAME} ${arg_CORPUS})
    endif()
endfunction()
