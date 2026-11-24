# Loom step 7 on a swapchain: LoomDrawTest's graph into SwapchainFrameTarget, with --frames=N and --resize. It uses
# LoomDrawTest's cooked shaders and its graph source.
ADD_INTEGRATION_TEST(LoomTriangleTest
    "main.cpp"
    "../LoomDrawTest/LoomDrawGraph.hpp"
    "../LoomDrawTest/LoomDrawGraph.cpp"
    "../fixtures/FrameLimit.hpp"
    "../fixtures/FrameLimit.cpp"
    "../fixtures/ResizeTest.hpp"
    "../fixtures/ResizeTest.cpp"
    "../fixtures/WindowedBoot.hpp"
    "../fixtures/WindowedBoot.cpp")
add_dependencies(LoomTriangleTest loom_draw_test_shaders)
target_link_libraries(LoomTriangleTest PRIVATE bootstrap_vulkan loom_vulkan loom_vulkan_platform)
target_compile_definitions(LoomTriangleTest PRIVATE
    "LOOM_DRAW_TEST_MANIFEST=\"$<TARGET_PROPERTY:loom_draw_test_shaders,LODESTONE_MANIFEST>\"")
target_include_directories(LoomTriangleTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../LoomDrawTest")
