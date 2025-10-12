cmake_minimum_required(VERSION 3.10)

# Foundation unit tests
set(FOUNDATION_TEST_SOURCES
    test_main.cpp
    
    # Threading tests
    threading/test_atomic128.cpp
    threading/test_critical_section.cpp
    threading/test_srw_lock.cpp
    threading/test_exponential_backoff_sleeper.cpp
    
    # Container tests
    containers/test_circular_buffer.cpp
    containers/test_mwsr_queue.cpp
    
    # Utility tests
    utility/test_delegate.cpp
    utility/test_multicast_delegate.cpp
    utility/test_murmur_hash.cpp
    utility/test_tagged_bool.cpp
    utility/test_source_paths.cpp
    
    # Math tests
    math/test_math.cpp
    math/test_float2.cpp
    math/test_float3.cpp
    math/test_float4.cpp
    math/test_integer_vectors.cpp
    math/test_vector_simd.cpp
    math/test_matrix_storage.cpp
    math/test_matrix_simd.cpp
    math/test_matrix_projection.cpp
    math/test_math_integration.cpp

)

# Create test target
add_unit_test_target(foundation_tests 
    "${FOUNDATION_TEST_SOURCES}"
    "foundation")

# Set include directories for foundation tests
target_include_directories(foundation_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/foundation/include"
    "${CMAKE_SOURCE_DIR}/third_party"
    "${CMAKE_SOURCE_DIR}/third_party/glm"
    ${Vulkan_INCLUDE_DIR})
