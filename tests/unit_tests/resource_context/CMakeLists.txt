cmake_minimum_required(VERSION 3.10)

# RhiResources unit tests. These run against a real headless device with full validation
set(RESOURCE_CONTEXT_TEST_SOURCES
    test_main.cpp
    ResourceContextFixture.hpp
    ResourceContextFixture.cpp
    test_buffers.cpp
    test_images.cpp
    test_concurrency.cpp
)

set(resource_context_test_link_targets
    foundation
    rhi_system
    rhi_resources)

add_unit_test_target(resource_context_tests
    "${RESOURCE_CONTEXT_TEST_SOURCES}"
    "${resource_context_test_link_targets}")
