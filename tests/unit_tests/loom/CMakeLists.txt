cmake_minimum_required(VERSION 3.10)

set(LOOM_TEST_SOURCES
    test_main.cpp
    test_diagnostics.cpp
    test_builder.cpp
    freeze_test_helpers.hpp
    test_frozen_graph.cpp
    test_freeze_resolve.cpp
    test_freeze_draw.cpp
    test_order.cpp
)

add_unit_test_target(loom_tests
    "${LOOM_TEST_SOURCES}"
    "loom")

# The stages and their helpers live in modules/RenderGraph/src. Tests feed them directly.
target_include_directories(loom_tests PRIVATE "${CMAKE_SOURCE_DIR}/modules/RenderGraph/src")
