
ADD_MODULE(platform_system
    "include/PlatformSurface.hpp"
    "include/PlatformSystem.hpp"
    "include/PlatformTypes.hpp"
    "include/Swapchain.hpp"
    "src/PlatformSurface.cpp"
    "src/PlatformSystem.cpp"
    "src/PlatformSystemImpl.hpp"
    "src/PlatformSystemImpl.cpp"
    "src/Swapchain.cpp"
    "src/SwapchainImpl.cpp"
    "src/SwapchainImpl.hpp")

target_link_libraries(platform_system PUBLIC foundation glfw ${Vulkan_LIBRARY} rhi_system)
target_include_directories(platform_system PRIVATE ${Vulkan_INCLUDE_DIR} "../../third_party/json/include" "../RhiSystem/include")

if (WIN32)
    target_sources(platform_system PRIVATE "src/Win32/PlatformDisplayInfo.hpp" "src/Win32/PlatformDisplayInfo.cpp")
    target_include_directories(platform_system PRIVATE "src/Win32")
    target_link_libraries(platform_system PRIVATE dxgi)
    # DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO_2 needs the Windows 11 SDK declarations
    target_compile_definitions(platform_system PRIVATE NTDDI_VERSION=0x0A00000F)
elseif(UNIX)
    target_sources(platform_system PRIVATE "src/Unix/PlatformDisplayInfo.hpp" "src/Unix/PlatformDisplayInfo.cpp")
    target_include_directories(platform_system PRIVATE "src/Unix")
endif()
