cmake_minimum_required(VERSION 3.10)

set(COLOR_PIPE_TEST_SOURCES
    test_main.cpp
    test_color_spaces.cpp
    test_transfer_functions.cpp
    test_display_target.cpp
    test_color_composite.cpp
)

add_unit_test_target(color_pipe_tests
    "${COLOR_PIPE_TEST_SOURCES}"
    "color_math;color_pipe")
