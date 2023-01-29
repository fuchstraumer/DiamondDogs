

ADD_MODULE(rendering_context
    "include/PipelineExecutableInfo.hpp"
    "include/PlatformWindow.hpp"
    "include/RenderingContext.hpp"
    "src/PipelineExecutableInfo.cpp"
    "src/PlatformWindow.cpp"
    "src/RenderingContext.cpp"
)

TARGET_LINK_LIBRARIES(rendering_context PUBLIC vpr_core glfw ${Vulkan_LIBRARY})
TARGET_INCLUDE_DIRECTORIES(rendering_context PUBLIC ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    "../../third_party/json/include" ${Vulkan_INCLUDE_DIR})
