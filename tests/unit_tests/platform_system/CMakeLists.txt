cmake_minimum_required(VERSION 3.10)

set(PLATFORM_SYSTEM_TEST_SOURCES
    test_main.cpp
    test_platform_system.cpp
    test_swapchain.cpp)

add_unit_test_target(platform_system_tests
    "${PLATFORM_SYSTEM_TEST_SOURCES}"
    "platform_system")

target_include_directories(platform_system_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/modules/PlatformSystem/include"
    "${CMAKE_SOURCE_DIR}/foundation/include"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/third_party/json"
    "${Vulkan_INCLUDE_DIR}")
