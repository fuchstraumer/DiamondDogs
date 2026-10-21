# Lodestone cooks DiamondDogs' shaders at build time and provides the manifest client that the runtime
# reads them with. DiamondDogs has no Slang of its own: the cooker is the only Slang user, and it runs at
# build time.
#
# Lodestone is consumed as a sibling checkout by default, because both repositories are developed together.
# Point DIAMOND_DOGS_LODESTONE_DIR elsewhere to use another checkout.

set(DIAMOND_DOGS_LODESTONE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Lodestone" CACHE PATH
    "Path to the Lodestone checkout that cooks shaders and provides the manifest client")
cmake_path(ABSOLUTE_PATH DIAMOND_DOGS_LODESTONE_DIR NORMALIZE)

if (NOT EXISTS "${DIAMOND_DOGS_LODESTONE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Lodestone was not found at ${DIAMOND_DOGS_LODESTONE_DIR}. Clone https://github.com/fuchstraumer/Lodestone "
        "beside DiamondDogs (with `git submodule update --init --recursive`), or set DIAMOND_DOGS_LODESTONE_DIR.")
endif()

# DiamondDogs is a Vulkan engine: it cooks SPIR-V only, so it needs no Tint, and no Dawn checkout to fetch
set(LODESTONE_ENABLE_WGSL OFF CACHE BOOL "DiamondDogs cooks SPIR-V only" FORCE)
set(LODESTONE_BUILD_COOKER ON CACHE BOOL "DiamondDogs cooks its shaders as a build step")

# Slang's SPIRV-Tools build adds its own mimalloc-static target (Slang's pinned v2.1.7), and the root CMakeLists
# only adds our checkout (v3.0.7) when Slang did not. We keep Slang's: with our v3.0.7, every Debug executable
# hits mimalloc's page-map assert at startup (page-map.c:339, measured 2026-10-21), because foundation's
# new/delete override allocates before the page map exists. Lodestone leaves the options ON when we don't set
# them. To use our copy, set SLANG_ENABLE_MIMALLOC, SLANG_ENABLE_SPIRV_TOOLS_MIMALLOC, SPIRV_TOOLS_USE_MIMALLOC
# and SPIRV_TOOLS_USE_MIMALLOC_IN_STATIC_BUILD OFF here (SPIRV-Tools defaults its two ON on Windows), and fix
# that assert first.
unset(SLANG_ENABLE_MIMALLOC CACHE)
unset(SLANG_ENABLE_SPIRV_TOOLS_MIMALLOC CACHE)
unset(SPIRV_TOOLS_USE_MIMALLOC CACHE)
unset(SPIRV_TOOLS_USE_MIMALLOC_IN_STATIC_BUILD CACHE)

# Fixed TLS must stay off. When several allocator instances share a process (slang.dll and each executable),
# fixed TLS makes all of them claim the same TLS slot. slang-bootstrap crashed with an access violation while
# generating the core module when this was on (measured 2026-10-19).
set(MI_WIN_USE_FIXED_TLS OFF CACHE BOOL "Must stay off: several mimalloc instances can share a process" FORCE)

add_subdirectory("${DIAMOND_DOGS_LODESTONE_DIR}" "${CMAKE_BINARY_DIR}/third_party/lodestone")

if (TARGET lodestone_cooker_console)
    set_target_properties(lodestone_cooker_console PROPERTIES FOLDER "third_party/lodestone")
endif()
foreach(lodestoneTarget IN ITEMS lodestone lodestone_client_library lodestone_client_header_check lodestone_json lodestone_suggest)
    if (TARGET ${lodestoneTarget})
        set_target_properties(${lodestoneTarget} PROPERTIES FOLDER "third_party/lodestone")
    endif()
endforeach()
