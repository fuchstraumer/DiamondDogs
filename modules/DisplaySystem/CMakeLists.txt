
ADD_MODULE(display_system
    "include/DisplaySystem.hpp"
    "include/DisplaySystemTypes.hpp"
    "include/PlatformSurface.hpp"
    "include/Swapchain.hpp"
    "src/DisplaySystem.cpp"
    "src/PlatformSurface.cpp"
    "src/Swapchain.cpp"
    "src/SwapchainImpl.hpp"
    "src/SwapchainImpl.cpp"
    "src/Tonemapper.hpp")

target_link_libraries(display_system PUBLIC glfw ${Vulkan_LIBRARY} platform_system)
target_include_directories(display_system PRIVATE ${Vulkan_INCLUDE_DIR} "../PlatformSystem/include")
