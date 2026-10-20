cmake_minimum_required(VERSION 3.10)

# RhiResources unit tests
set(RESOURCE_CONTEXT_TEST_SOURCES
    test_main.cpp
    test_resource_loader.cpp
    test_resource_cache.cpp
    test_resource_handles.cpp
    test_concurrent_resource_access.cpp
    test_memory_management.cpp
)

set(resource_context_test_link_targets
    foundation
    rhi_resources)

add_unit_test_target(resource_context_tests
    "${RESOURCE_CONTEXT_TEST_SOURCES}"
    "${resource_context_test_link_targets}")

target_include_directories(resource_context_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/modules/RhiResources/include"
    "${CMAKE_SOURCE_DIR}/modules/RhiSystem/include"
    "${CMAKE_SOURCE_DIR}/foundation/include"
    "${CMAKE_SOURCE_DIR}/third_party"
    ${Vulkan_INCLUDE_DIR})
