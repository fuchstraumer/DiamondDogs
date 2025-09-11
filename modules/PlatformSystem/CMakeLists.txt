
ADD_MODULE(platform_system
    "include/PlatformSystem.hpp"
    "include/PlatformTypes.hpp"
    "src/PlatformSystem.cpp"
    "src/PlatformSystemImpl.hpp"
    "src/PlatformSystemImpl.cpp")

target_link_libraries(platform_system PUBLIC foundation glfw ${Vulkan_LIBRARY})
target_include_directories(platform_system PRIVATE ${Vulkan_INCLUDE_DIR} "../../third_party/json/include")
