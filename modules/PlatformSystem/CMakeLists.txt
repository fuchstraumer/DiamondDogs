
ADD_MODULE(platform_system
    "include/PlatformSystem.hpp"
    "include/PlatformTypes.hpp"
    "src/PlatformSystem.cpp"
    "src/PlatformSystemImpl.hpp"
    "src/PlatformSystemImpl.cpp")

target_link_libraries(platform_system PUBLIC foundation glfw ${Vulkan_LIBRARY})
target_include_directories(platform_system PRIVATE ${Vulkan_INCLUDE_DIR} "../../third_party/json/include")

if (WIN32)
    target_sources(platform_system PRIVATE "src/Win32/HDRSupport.hpp" "src/Win32/HDRSupport.cpp")
    target_include_directories(platform_system PRIVATE "src/Win32")
elseif(UNIX)
    target_sources(platform_system PRIVATE "src/Unix/HDRSupport.hpp" "src/Unix/HDRSupport.cpp")
    target_include_directories(platform_system PRIVATE "src/Unix")
endif()
