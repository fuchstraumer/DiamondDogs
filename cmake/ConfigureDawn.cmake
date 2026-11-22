set(DAWN_FETCH_DEPENDENCIES OFF)
set(DAWN_BUILD_GEN_DIR "${CMAKE_BINARY_DIR}/third_party/dawn/gen")
# Turn off every backend and tool that the Emscripten build does not use
foreach(dawnOption IN ITEMS
    DAWN_ENABLE_DESKTOP_GL DAWN_ENABLE_OPENGLES DAWN_ENABLE_D3D11 DAWN_ENABLE_D3D12 DAWN_ENABLE_VULKAN
    DAWN_ENABLE_SPIRV_VALIDATION DAWN_BUILD_SAMPLES DAWN_BUILD_TESTS DAWN_BUILD_PROTOBUF DAWN_BUILD_NODE_BINDINGS
    DAWN_BUILD_BENCHMARKS DAWN_BUILD_FUZZERS DAWN_USE_WINDOWS_UI DAWN_USE_GLFW
    TINT_BUILD_FUZZERS TINT_BUILD_DOCS TINT_BUILD_TESTS TINT_BUILD_SAMPLES TINT_BUILD_CMD_TOOLS
    TINT_BUILD_GLSL_VALIDATOR TINT_BUILD_SPV_WRITER TINT_BUILD_SPV_READER TINT_ENABLE_IR_DUMPING TINT_BUILD_IR_BINARY)
    set(${dawnOption} OFF)
endforeach()

set(DIAMOND_DOGS_DAWN_DIR "" CACHE PATH
    "Path to a local Dawn checkout. Empty uses the emdawnwebgpu package pinned in cmake/dawn-lock.json")

if(DIAMOND_DOGS_DAWN_DIR STREQUAL "")
    # Pinned release package: update it with scripts/sync_dawn.py
    include(FetchContent)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/dawn-lock.json" diamondDogsDawnLock)
    string(JSON diamondDogsDawnUrl GET "${diamondDogsDawnLock}" url)
    string(JSON diamondDogsDawnSha256 GET "${diamondDogsDawnLock}" sha256)
    FetchContent_Declare(emdawnwebgpu_pkg
        URL "${diamondDogsDawnUrl}"
        URL_HASH "SHA256=${diamondDogsDawnSha256}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(emdawnwebgpu_pkg)
    file(GLOB_RECURSE diamondDogsDawnPort "${emdawnwebgpu_pkg_SOURCE_DIR}/emdawnwebgpu.port.py")
    list(GET diamondDogsDawnPort 0 diamondDogsDawnPortPath)
    if(NOT diamondDogsDawnPortPath)
        message(FATAL_ERROR "emdawnwebgpu.port.py was not found in ${emdawnwebgpu_pkg_SOURCE_DIR}")
    endif()
    add_library(emdawnwebgpu_cpp INTERFACE)
    target_compile_options(emdawnwebgpu_cpp INTERFACE "--use-port=${diamondDogsDawnPortPath}")
    target_link_options(emdawnwebgpu_cpp INTERFACE "--use-port=${diamondDogsDawnPortPath}")
    # The port adds these itself at compile time. Listing them lets clangd find the headers
    cmake_path(GET diamondDogsDawnPortPath PARENT_PATH diamondDogsDawnPkgDir)
    target_include_directories(emdawnwebgpu_cpp SYSTEM INTERFACE
        "${diamondDogsDawnPkgDir}/webgpu/include" "${diamondDogsDawnPkgDir}/webgpu_cpp/include")
else()
    cmake_path(ABSOLUTE_PATH DIAMOND_DOGS_DAWN_DIR NORMALIZE)
    if (NOT EXISTS "${DIAMOND_DOGS_DAWN_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Dawn was not found at ${DIAMOND_DOGS_DAWN_DIR}. Set DIAMOND_DOGS_DAWN_DIR.")
    endif()
    add_subdirectory("${DIAMOND_DOGS_DAWN_DIR}" "${CMAKE_BINARY_DIR}/third_party/dawn" EXCLUDE_FROM_ALL)
endif()

if(TARGET emdawnwebgpu_c)
    # emsdk 7.x libc++ stopped pulling <string> transitively in Dawn's webgpu.cpp
    set(diamondDogsDawnStringFix "${CMAKE_BINARY_DIR}/generated/dawn_string_fix.hpp")
    file(WRITE "${diamondDogsDawnStringFix}" "#pragma once\n#include <string>\n")
    target_compile_options(emdawnwebgpu_c PRIVATE -include "${diamondDogsDawnStringFix}")
endif()
