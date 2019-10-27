
ADD_MODULE(vtf_module
    "include/VTF_Scene.hpp"
    "include/vtfFrameData.hpp"
    "include/vtfStructs.hpp"
    "include/vtfTasksAndSteps.hpp"
    "src/VTF_Scene.cpp"
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
    "../rendergraph_module/include")

TARGET_LINK_LIBRARIES(vtf_module PRIVATE ShaderTools glfw ${Vulkan_LIBRARY}
    vpr_core vpr_resource vpr_sync vpr_command vpr_render easyloggingpp mango)