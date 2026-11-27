set(BOOTSTRAP_VULKAN_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/DeviceStage.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/GpuStage.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SpineNeeds.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/WindowStage.hpp"
)

set(BOOTSTRAP_VULKAN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/DeviceStage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/GpuStage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SpineNeeds.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanCapabilities.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanCapabilities.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/WindowStage.cpp"
)

add_module(bootstrap_vulkan
    ${BOOTSTRAP_VULKAN_HEADERS}
    ${BOOTSTRAP_VULKAN_SOURCES}
)

# the device stages on Vulkan. An executable links one backend, because this target defines GpuStartContext
target_include_directories(bootstrap_vulkan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(bootstrap_vulkan PUBLIC bootstrap rhi_context rhi_system platform_system rhi_resources)

# platform_system does not export its folder. nlohmann stays private to DeviceConfig.cpp
target_include_directories(bootstrap_vulkan PRIVATE
    "${PROJECT_SOURCE_DIR}/modules/PlatformSystem/include"
    "${PROJECT_SOURCE_DIR}/third_party/json/include")
