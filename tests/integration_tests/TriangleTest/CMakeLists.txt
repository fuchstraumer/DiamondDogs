
ADD_INTEGRATION_TEST(TriangleTest
    "main.cpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/CommonCreationFunctions.hpp"
    "../fixtures/CommonCreationFunctions.cpp"
    "../fixtures/FrameLimit.hpp"
    "../fixtures/FrameLimit.cpp"
    "../fixtures/ResizeTest.hpp"
    "../fixtures/ResizeTest.cpp"
    "../fixtures/WindowedBoot.hpp"
    "../fixtures/WindowedBoot.cpp"
    "TriangleTest.cpp"
    "TriangleTest.hpp"
)
target_link_libraries(TriangleTest PRIVATE bootstrap_vulkan)

