cmake_minimum_required(VERSION 3.10)

# Color pipe unit tests: the CPU reference of the HDR pipe's color math
set(COLOR_PIPE_TEST_SOURCES
    test_main.cpp
    test_color_spaces.cpp
    test_transfer_functions.cpp
)

add_unit_test_target(color_pipe_tests
    "${COLOR_PIPE_TEST_SOURCES}"
    "color_pipe")
