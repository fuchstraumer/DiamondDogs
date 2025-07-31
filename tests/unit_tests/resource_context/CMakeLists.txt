cmake_minimum_required(VERSION 3.10)

# Resource Context unit tests
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
    resource_context)

# Create test target
add_unit_test_target(resource_context_tests 
    "${RESOURCE_CONTEXT_TEST_SOURCES}"
    "${resource_context_test_link_targets}")  # Will need to link to actual resource context module when available

# Set include directories for resource context tests
target_include_directories(resource_context_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/modules/ResourceContext/include"
    "${CMAKE_SOURCE_DIR}/foundation/include"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/third_party/glm"
    "${CMAKE_SOURCE_DIR}/third_party/vulpesrender/resource/include"
    ${Vulkan_INCLUDE_DIR})

