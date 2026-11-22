cmake_minimum_required(VERSION 3.10)

# Foundation unit tests
set(FOUNDATION_TEST_SOURCES
    test_main.cpp

    # Async tests
    async/test_post_and_yield.cpp
    async/test_task_scheduler.cpp

    # Content key tests
    content/test_bytes_sink.cpp
    content/test_encode.cpp
    content/test_encode_erased.cpp
    content/test_json_sink.cpp
    content/test_keys.cpp
    content/test_text_sink.cpp

    # Reflection tests
    reflect/test_describe.cpp
    reflect/test_type_info.cpp
    reflect/test_value_access.cpp

    # Timing tests
    timing/test_frame_limiter.cpp
    timing/test_high_resolution_wait.cpp
    timing/test_monotonic_clock.cpp

    # Container tests
    containers/LifetimeTracked.cpp
    containers/test_circular_buffer.cpp
    containers/test_lru_cache.cpp
    containers/test_ring_allocator.cpp
    containers/test_small_vector.cpp
    
    # Utility tests
    utility/test_delegate.cpp
    utility/test_multicast_delegate.cpp
    utility/test_murmur_hash.cpp
    utility/test_tagged_bool.cpp
    utility/test_xxhash.cpp
    
    # Math tests
    math/MathTests.cpp
    math/MathStorageTests.cpp
)

if(NOT EMSCRIPTEN)
    list(APPEND FOUNDATION_TEST_SOURCES
        async/test_frame_heap.cpp
        containers/test_mwsr_queue.cpp
        net/test_socket.cpp
        threading/test_atomic128.cpp
        threading/test_critical_section.cpp
        threading/test_srw_lock.cpp
        threading/test_exponential_backoff_sleeper.cpp
        utility/test_mimalloc_override.cpp
        utility/test_source_paths.cpp
    )
endif()

# The shared PCH includes DirectXMath.h, which would hide a SIMD include in MathStorage.hpp.
# This file must compile without it.
set_source_files_properties(math/MathStorageTests.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# Create test target
add_unit_test_target(foundation_tests 
    "${FOUNDATION_TEST_SOURCES}"
    "foundation")

# Set include directories for foundation tests
target_include_directories(foundation_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/foundation/include"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/third_party/glm")

if(NOT EMSCRIPTEN)
    target_include_directories(foundation_tests PRIVATE ${Vulkan_INCLUDE_DIR})
endif()

# xxHash comes from Lodestone's checkout (see cmake/ConfigureLodestone.cmake). Only this test links it for now.
target_link_libraries(foundation_tests xxHash::xxhash)
