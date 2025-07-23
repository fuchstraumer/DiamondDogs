
set(imgui_source_files
    "../../third_party/imgui/imgui.h"
    "../../third_party/imgui/imgui.cpp"
    "../../third_party/imgui/imgui_draw.cpp"
    "../../third_party/imgui/imconfig.h"
    "../../third_party/imgui/imgui_demo.cpp"
    "../../third_party/imgui/imgui_internal.h"
    "../../third_party/imgui/imgui_widgets.cpp")

source_group("imgui_ext" FILES ${imgui_source_files})

ADD_MODULE(imgui_module
    "include/ImGuiModule.hpp"
    ${imgui_source_files}
    "src/ImGuiModule.cpp"
    "src/ImGuiWrapper.hpp"
    "src/ImGuiWrapper.cpp")

target_link_libraries(imgui_module PRIVATE glfw ${Vulkan_LIBRARY} vpr_resource)

target_include_directories(imgui_module PRIVATE "../../third_party/" "../../third_party/imgui"
    "../rendering_context/include" "../resource_context/include" ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    ${Vulkan_INCLUDE_DIR})