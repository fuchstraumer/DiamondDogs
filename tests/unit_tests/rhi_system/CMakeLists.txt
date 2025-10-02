cmake_minimum_required(VERSION 3.10)

# Rendering Context unit tests
set(RENDERING_CONTEXT_TEST_SOURCES
    TestJsonConfig.cpp
    TestMain.cpp
    TestRhiSystem.cpp
    TestShaderObject.cpp)

# Create test target
add_unit_test_target(rhi_system_tests 
    "${RENDERING_CONTEXT_TEST_SOURCES}"
    "foundation;rhi_system")

# Set include directories for rendering context tests
target_include_directories(rhi_system_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/Modules/RhiSystem/include"
    "${CMAKE_SOURCE_DIR}/foundation/include"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/third_party/json/include"
    ${Vulkan_INCLUDE_DIR})
