cmake_minimum_required(VERSION 3.10)

set(INPUT_SYSTEM_TEST_SOURCES
    test_main.cpp
    test_types.cpp
    test_frame.cpp
    test_log.cpp
)

add_unit_test_target(input_system_tests
    "${INPUT_SYSTEM_TEST_SOURCES}"
    "input_system")
