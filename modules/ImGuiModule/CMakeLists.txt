
set(imgui_source_files
    "../../third_party/imgui/imgui.h"
    "../../third_party/imgui/imgui.cpp"
    "../../third_party/imgui/imgui_draw.cpp"
    "../../third_party/imgui/imconfig.h"
    "../../third_party/imgui/imgui_demo.cpp"
    "../../third_party/imgui/imgui_internal.h"
    "../../third_party/imgui/imgui_tables.cpp"
    "../../third_party/imgui/imgui_widgets.cpp"
    "../../third_party/imgui/backends/imgui_impl_glfw.h"
    "../../third_party/imgui/backends/imgui_impl_glfw.cpp"
    "../../third_party/imgui/backends/imgui_impl_vulkan.h"
    "../../third_party/imgui/backends/imgui_impl_vulkan.cpp")

source_group("imgui_ext" FILES ${imgui_source_files})

ADD_MODULE(imgui_module
    "include/ImGuiModule.hpp"
    ${imgui_source_files}
    "src/ImGuiModule.cpp")

# the Dear ImGui sources are third-party code, so they compile without our PCH
set_source_files_properties(${imgui_source_files} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_link_libraries(imgui_module PUBLIC rhi_system glfw ${Vulkan_LIBRARY})

# users call ImGui:: directly, so the imgui folder is public
target_include_directories(imgui_module PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/imgui"
    ${Vulkan_INCLUDE_DIR})
