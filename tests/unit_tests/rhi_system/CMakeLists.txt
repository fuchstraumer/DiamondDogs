cmake_minimum_required(VERSION 3.10)

# Rendering Context unit tests
set(RENDERING_CONTEXT_TEST_SOURCES
    TestCommandBuffer.cpp
    TestGpuTimeline.cpp
    TestGpuTimers.cpp
    TestExecutableStatistics.cpp
    TestMain.cpp
    TestRhiSystem.cpp
    TestRhiSystemStart.cpp
    TestShaderBundle.cpp
    TestShaderProgram.cpp
    ../fixtures/StartTestRhi.hpp
    ../fixtures/StartTestRhi.cpp)

# Create test target
add_unit_test_target(rhi_system_tests 
    "${RENDERING_CONTEXT_TEST_SOURCES}"
    "foundation;rhi_system")

# Set include directories for rendering context tests
target_include_directories(rhi_system_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/Modules/RhiSystem/core/include"
    "${CMAKE_SOURCE_DIR}/Modules/RhiSystem/objects/include"
    "${CMAKE_SOURCE_DIR}/Modules/RhiSystem/shaders/include"
    # TestRhiSystemStart tests the device planner, which is internal to rhi_system
    "${CMAKE_SOURCE_DIR}/modules/RhiSystem/src/core"
    "${CMAKE_SOURCE_DIR}/tests/unit_tests/fixtures"
    "${CMAKE_SOURCE_DIR}/foundation/include"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/third_party/json/include"
    ${Vulkan_INCLUDE_DIR})

# TestShaderBundle opens the HashFill bundle that ComputeManifestTest cooks, with no device
add_dependencies(rhi_system_tests compute_manifest_test_shaders)
target_compile_definitions(rhi_system_tests PRIVATE
    "HASH_FILL_BUNDLE=\"$<TARGET_PROPERTY:compute_manifest_test_shaders,LODESTONE_MANIFEST>\"")
