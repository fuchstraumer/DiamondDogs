cmake_minimum_required(VERSION 3.10)

# Foundation unit tests
set(FOUNDATION_TEST_SOURCES
    test_main.cpp
    
    # Threading tests
    threading/test_atomic128.cpp
    threading/test_mcas.cpp
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
    
    # Math tests
    math/test_math.cpp

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
