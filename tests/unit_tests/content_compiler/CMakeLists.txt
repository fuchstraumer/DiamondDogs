cmake_minimum_required(VERSION 3.10)

set(CONTENT_COMPILER_TEST_SOURCES
    test_main.cpp
    test_kinds.hpp
    test_kinds.cpp
    test_params_box.cpp
    test_kind_registry.cpp
    test_recipe_table.cpp
    test_ingest.cpp
)

add_unit_test_target(content_compiler_tests
    "${CONTENT_COMPILER_TEST_SOURCES}"
    "content_compiler")

# the batch collision check is internal to the module, and a test feeds it forged keys
target_include_directories(content_compiler_tests PRIVATE "${CMAKE_SOURCE_DIR}/modules/ContentCompiler/src")
