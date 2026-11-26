set(EDITOR_UI_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UiTheme.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UiLayout.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/UiControls.hpp"
)

set(EDITOR_UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiTheme.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiLayout.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UiControls.cpp"
)

add_module(editor_ui
    ${EDITOR_UI_HEADERS}
    ${EDITOR_UI_SOURCES}
)

target_link_libraries(editor_ui PUBLIC imgui_module)

target_include_directories(editor_ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")
