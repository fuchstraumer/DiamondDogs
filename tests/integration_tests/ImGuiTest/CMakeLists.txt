# Windowed smoke test for ImGuiModule: the Dear ImGui demo window over a cleared swapchain image
ADD_INTEGRATION_TEST(ImGuiTest
    "main.cpp"
    "ImGuiScene.hpp"
    "ImGuiScene.cpp"
    "../fixtures/VulkanScene.hpp"
    "../fixtures/VulkanScene.cpp"
    "../fixtures/FrameLimit.hpp"
    "../fixtures/FrameLimit.cpp"
)
target_link_libraries(ImGuiTest PRIVATE imgui_module)
