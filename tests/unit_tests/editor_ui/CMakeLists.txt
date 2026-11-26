cmake_minimum_required(VERSION 3.10)

set(EDITOR_UI_TEST_SOURCES
    test_main.cpp
    test_layout.cpp
)

add_unit_test_target(editor_ui_tests
    "${EDITOR_UI_TEST_SOURCES}"
    "editor_ui")

target_include_directories(editor_ui_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/modules/EditorUi/include"
    "${CMAKE_SOURCE_DIR}/foundation/include"
    "${CMAKE_SOURCE_DIR}/third_party/imgui")
