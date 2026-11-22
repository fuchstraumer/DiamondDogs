CMAKE_MINIMUM_REQUIRED(VERSION 3.10)
PROJECT(DiamondDogs)
# and turquoise days...

# clangd reads compile_commands.json. Set it here too, so a configure without a preset still writes it
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC is also true for clang-cl, which takes the same flags but has no /MP and no SVML library. Test
# DIAMOND_DOGS_COMPILER_IS_CL for those two, and MSVC for everything cl-style.
# clang-cl ignores /JMC without debug info, and warns about it, so it goes only where /Zi is.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    SET(DIAMOND_DOGS_COMPILER_IS_CL ON)
else()
    SET(DIAMOND_DOGS_COMPILER_IS_CL OFF)
endif()

if(MSVC)
    SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi /Oi /Gy /Ob3")
    SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /JMC /Gy")
    SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /JMC /Gy")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /bigobj")
    if(DIAMOND_DOGS_COMPILER_IS_CL)
        SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    endif()
endif()

if(EMSCRIPTEN)
    # Dawn first: the flags below apply only to directories added after them
    include(cmake/ConfigureDawn.cmake)
    add_compile_options(-fno-exceptions -msimd128)
    add_compile_definitions(JSON_NOEXCEPTION)
endif()

# Generate header with source tree paths for tests
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SourcePaths.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/SourcePaths.hpp"
    @ONLY
)

if(NOT EMSCRIPTEN)
    FIND_PACKAGE(Vulkan REQUIRED)
endif()

# mimalloc: our checkout (third_party/mimalloc) is the only allocator that DiamondDogs executables link. These options
# go first because the cache entries decide how the mimalloc-static target is built
SET(MI_BUILD_OBJECT OFF CACHE BOOL "Build object library")
SET(MI_BUILD_SHARED OFF CACHE BOOL "Build shared library")
SET(MI_BUILD_TESTS OFF CACHE BOOL "Build test executables")
SET(MI_WIN_REDIRECT OFF CACHE BOOL "Use redirection module ('mimalloc-redirect') on Windows if compiling mimalloc as a DLL")

# Precompiled headers for our own targets (never for third_party): see cmake/DiamondDogsPch.cmake
include(cmake/DiamondDogsPch.cmake)

# Lodestone: the shader cooker, the manifest client, and the Slang compiler target
include(cmake/ConfigureLodestone.cmake)

# Lodestone builds Slang in its own tree, so no other project defines mimalloc-static in this build. If one does, two
# allocators would override new and delete in one process.
if(NOT EMSCRIPTEN)
    if (TARGET mimalloc-static)
        get_target_property(mimallocSourceDir mimalloc-static SOURCE_DIR)
        message(FATAL_ERROR
            "mimalloc-static is already defined (source directory: ${mimallocSourceDir}). DiamondDogs adds its own "
            "third_party/mimalloc, and only one mimalloc-static can exist. Find the project that adds it (Lodestone's Slang "
            "build does that when LODESTONE_ISOLATE_SLANG is OFF) and stop it.")
    endif()
    add_subdirectory(third_party/mimalloc)
    set_target_properties(mimalloc-static PROPERTIES FOLDER "third_party")

    add_subdirectory(third_party/vma)
endif()

add_subdirectory(third_party/entt)
if(NOT EMSCRIPTEN)
    SET(GLI_TEST_ENABLE OFF CACHE BOOL "Build unit tests")
    # gli asks for CMake 3.1, and CMake 4 removed compatibility with anything below 3.5
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    add_subdirectory(third_party/gli)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    set_target_properties(gli PROPERTIES FOLDER "third_party")
    set_target_properties(gli_dummy PROPERTIES FOLDER "third_party")

    SET(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation")
    SET(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs")
    SET(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs")
    set(GLFW_LIBRARY_TYPE "STATIC" CACHE STRING "Library type override for GLFW (SHARED, STATIC, OBJECT, or empty to follow BUILD_SHARED_LIBS)")
    add_subdirectory(third_party/glfw)
    set_target_properties(glfw PROPERTIES FOLDER "third_party")
endif()

# Foundation is core functionality that wouldn't make sense to put into modules:
# Threading primitives, utilities, exception helpers, containers, etc
# Also our building block reactor objects, used to build higher-level systems
ADD_SUBDIRECTORY(foundation)
ADD_SUBDIRECTORY(modules)
if(NOT EMSCRIPTEN)
    ADD_SUBDIRECTORY(applications)
endif()

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
if(EMSCRIPTEN)
    ADD_SUBDIRECTORY(tests/integration_tests/WebGpuTriangleTest)
else()
    ADD_SUBDIRECTORY(tests/integration_tests)
endif()
