
ADD_INTEGRATION_TEST(DescriptorTest
    "main.cpp"
    "DescriptorTest.cpp"
    "DescriptorTest.hpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp"
)

TARGET_LINK_LIBRARIES(DescriptorTest PRIVATE rendering_context rendergraph_module resource_context vpr_resource vpr_alloc vpr_core vpr_sync ${Vulkan_LIBRARY} glfw)
TARGET_INCLUDE_DIRECTORIES(DescriptorTest PRIVATE "../../../modules/rendering_context/include" "../../../modules/resource_context/include" 
    "../../../modules/rendergraph_module/include" ${Vulkan_INCLUDE_DIR} "../../../third_party/gli")
