
ADD_MODULE(vtf_module
    "include/vtfFrameData.hpp"
    "include/vtfStructs.hpp"
    "include/vtfTasksAndSteps.hpp"
    "src/vtfFrameData.cpp"
    "src/vtfTasksAndSteps.cpp"
)

TARGET_INCLUDE_DIRECTORIES(vtf_module PRIVATE
    ${VPR_INCLUDE_DIRS_PLUGIN_RELATIVE}
    ${Vulkan_INCLUDE_DIR}
    "../../third_party/svs"
    "../../third_party/easyloggingpp/src"
    "../../third_party/glm/"
    "../rendering_context/include"
    "../resource_context/include"
    "../material_module/include")

TARGET_LINK_LIBRARIES(vtf_module PRIVATE ShaderTools glfw ${Vulkan_LIBRARY}
    vpr_core vpr_resource vpr_sync vpr_command vpr_render easyloggingpp mango
    material_module)