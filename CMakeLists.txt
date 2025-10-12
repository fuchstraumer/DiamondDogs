CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
PROJECT(DiamondDogs)
# and turquoise days...

SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MP /Gy")
SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Oi /MP /Gy")
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Gy")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /JMC")

# Generate header with source tree paths for tests
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SourcePaths.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/SourcePaths.hpp"
    @ONLY
)

FIND_PACKAGE(Vulkan REQUIRED)

# Configure and organize Slang compiler for Vulkan/SPIR-V use
include(cmake/ConfigureSlang.cmake)
add_subdirectory(third_party/slang)
organize_slang_targets()

add_subdirectory(third_party/vma)
add_subdirectory(third_party/entt)
SET(GLI_TEST_ENABLE OFF CACHE BOOL "Build unit tests")
add_subdirectory(third_party/gli)
set_target_properties(gli PROPERTIES FOLDER "third_party")
set_target_properties(gli_dummy PROPERTIES FOLDER "third_party")
SET(MI_BUILD_OBJECT OFF CACHE BOOL "Build object library")
SET(MI_BUILD_SHARED OFF CACHE BOOL "Build shared library")
SET(MI_BUILD_TESTS OFF CACHE BOOL "Build test executables")
SET(MI_WIN_REDIRECT OFF CACHE BOOL "Use redirection module ('mimalloc-redirect') on Windows if compiling mimalloc as a DLL")
SET(MI_WIN_USE_FIXED_TLS ON CACHE BOOL "Use a fixed TLS slot on Windows to avoid extra tests in the malloc fast path")
add_subdirectory(third_party/mimalloc)
set_target_properties(mimalloc-static PROPERTIES FOLDER "third_party")

SET(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation")
SET(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs")
SET(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs")
set(GLFW_LIBRARY_TYPE "STATIC" CACHE STRING "Library type override for GLFW (SHARED, STATIC, OBJECT, or empty to follow BUILD_SHARED_LIBS)")
add_subdirectory(third_party/glfw)
set_target_properties(glfw PROPERTIES FOLDER "third_party")

# Foundation is core functionality that wouldn't make sense to put into modules:
# Threading primitives, utilities, exception helpers, containers, etc
# Also our building block reactor objects, used to build higher-level systems
ADD_SUBDIRECTORY(foundation)
ADD_SUBDIRECTORY(modules)

# Configure Google Test for unit testing
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/52eb8108c5bdec04579160ae17225d66034bd723.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing and add test directories
enable_testing()

set_target_properties(gtest PROPERTIES FOLDER "Tests/Gtest")
set_target_properties(gtest_main PROPERTIES FOLDER "Tests/Gtest")
set_target_properties(gmock PROPERTIES FOLDER "Tests/Gtest")
set_target_properties(gmock_main PROPERTIES FOLDER "Tests/Gtest")

ADD_SUBDIRECTORY(tests/unit_tests)
ADD_SUBDIRECTORY(tests/integration_tests)
