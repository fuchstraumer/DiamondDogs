
ADD_INTEGRATION_TEST(VTF_Scene
    "main.cpp"
    "VTF_Scene.hpp"
    "VTF_Scene.cpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp"
)

TARGET_INCLUDE_DIRECTORIES(VTF_Scene PRIVATE
    "../../../third_party/shadertools/include"
    "../../../modules/rendering_context/include"
    "../../../modules/resource_context/include"
    "../../../modules/rendergraph_module/include"
    "../../../third_party/easyloggingpp/src"
    "../../../third_party/glm/"
    ${Vulkan_INCLUDE_DIR}
    "../../../third_party/vulpesrender/core/include"
    "../../../third_party/vulpesrender/resource/include"
    "../../../third_party/vulpesrender/alloc/include"
    "../../../third_party/vulpesrender/command/include"
    "../../../third_party/vulpesrender/sync/include"
    "../../../third_party/vulpesrender/render/include"
)

TARGET_LINK_LIBRARIES(VTF_Scene PRIVATE ShaderTools rendering_context resource_context rendergraph_module glfw ${Vulkan_LIBRARY}
    vpr_core vpr_resource vpr_sync vpr_alloc vpr_command vpr_render easyloggingpp)
