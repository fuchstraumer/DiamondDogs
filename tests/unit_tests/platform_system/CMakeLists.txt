cmake_minimum_required(VERSION 3.10)

set(PLATFORM_SYSTEM_TEST_SOURCES
    TestMain.cpp
    TestPlatformSystem.cpp
    TestPlatformWindow.cpp
    TestSwapchain.cpp
    TestJsonConfig.cpp
    TestPlatformIntegration.cpp)

add_unit_test_target(platform_system_tests
    "${PLATFORM_SYSTEM_TEST_SOURCES}"
    "platform_system;foundation;rhi_system;glfw;${Vulkan_LIBRARY}")

target_include_directories(platform_system_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/Modules/PlatformSystem/include"
    "${CMAKE_SOURCE_DIR}/foundation/include"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/third_party/json/include"
    "${Vulkan_INCLUDE_DIR}")
