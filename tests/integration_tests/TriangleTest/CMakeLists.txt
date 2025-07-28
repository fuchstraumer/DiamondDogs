
ADD_INTEGRATION_TEST(TriangleTest
    "main.cpp"
    "TriangleTest.cpp"
    "TriangleTest.hpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp"
)

TARGET_LINK_LIBRARIES(TriangleTest PRIVATE rendering_context vpr_core vpr_sync ${Vulkan_LIBRARY} glfw)
TARGET_INCLUDE_DIRECTORIES(TriangleTest PRIVATE "../../../modules/RenderingContext/include" ${Vulkan_INCLUDE_DIR})
set_target_properties(TriangleTest PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)