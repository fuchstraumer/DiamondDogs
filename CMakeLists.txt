CMAKE_MINIMUM_REQUIRED(VERSION 3.4)
PROJECT(DiamondDogs)
# and turquoise days...

SET(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MP /Gy")
SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /Oi /MP /Gy")
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Gy")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /JMC")

function(add_mimalloc)
    set(MI_BUILD_TESTS OFF CACHE BOOL "Build test executables")
    set(MI_DEBUG_FULL ON CACHE BOOL "Use full internal heap invariant checking in DEBUG mode")
    set(MI_SECURE ON CACHE BOOL "Use full security mitigations (like guard pages, allocation randomization, double-free mitigation, and free-list corruption detection)")
    set(MI_USE_CXX ON CACHE BOOL "Use the C++ compiler to compile the library")
    set(MI_SEE_ASM ON CACHE BOOL "Generate assembly files")
    # needed since this is used in reactor that might also be present in other DLLs
    set(MI_LOCAL_DYNAMIC_TLS ON CACHE BOOL "Use slightly slower, dlopen-compatible TLS mechanism (Unix)")
    set(MI_BUILD_TESTS OFF CACHE BOOL "Build test executables")
    add_subdirectory(third_party/mimalloc)
endfunction()

FIND_PACKAGE(Vulkan REQUIRED)
ADD_SUBDIRECTORY(third_party/vulpesrender)
ADD_SUBDIRECTORY(third_party/shadertools)
SET(ENABLE_AVX2 ON CACHE BOOL "Enable AVX2 instructions")
ADD_SUBDIRECTORY(third_party/mango/build)
SET_TARGET_PROPERTIES(mango mango-framebuffer mango-opengl mango-vulkan PROPERTIES FOLDER "Mango")
SET(build_static_lib ON CACHE BOOL "Build easyloggingpp as a static library")
ADD_SUBDIRECTORY(third_party/easyloggingpp)
ADD_SUBDIRECTORY(third_party/snmalloc)
add_mimalloc()
# Foundation is core functionality that wouldn't make sense to put into modules:
# Threading primitives, ECS building blocks, utilitys, exception helpers, containers, etc
# Also our building block reactor objects, used to build higher-level systems
ADD_SUBDIRECTORY(foundation)
ADD_SUBDIRECTORY(modules)
ADD_SUBDIRECTORY(tests/integration_tests)
