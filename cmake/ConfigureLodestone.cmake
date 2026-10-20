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

# Lodestone only adds its FindVulkanHeaders module when it calls find_package(Vulkan) itself. We already
# did, so Slang's find_package(VulkanHeaders) would fail: the Vulkan SDK ships no VulkanHeaders config.
list(PREPEND CMAKE_MODULE_PATH "${DIAMOND_DOGS_LODESTONE_DIR}/cmake")

# Slang's SPIRV-Tools build adds its own mimalloc-static target, and ours has the same name, so only one of
# the two can exist. We take Slang's: it is the version Slang is tested against, and the root CMakeLists
# only adds our checkout when Slang did not. Lodestone forces Slang's mimalloc options on, so we can't turn
# Slang's copy off from here.
#
# Fixed TLS must stay off. slang.dll and every executable that links mimalloc-static each carry their own
# allocator instance, and fixed TLS makes all of them claim the same TLS slot. slang-bootstrap crashed
# with an access violation while generating the core module when this was on (measured 2026-10-19).
set(MI_WIN_USE_FIXED_TLS OFF CACHE BOOL "Must stay off: several mimalloc instances share each process" FORCE)

add_subdirectory("${DIAMOND_DOGS_LODESTONE_DIR}" "${CMAKE_BINARY_DIR}/third_party/lodestone")

if (TARGET lodestone_cooker_console)
    set_target_properties(lodestone_cooker_console PROPERTIES FOLDER "third_party/lodestone")
    # tools/cooker_console/CMakeLists.txt names its include folders with ${CMAKE_SOURCE_DIR}, which is our root
    # and not Lodestone's when Lodestone is a subdirectory. Remove this once Lodestone uses PROJECT_SOURCE_DIR.
    target_include_directories(lodestone_cooker_console PRIVATE
        "${DIAMOND_DOGS_LODESTONE_DIR}/include"
        "${DIAMOND_DOGS_LODESTONE_DIR}/tools/shared"
        "${DIAMOND_DOGS_LODESTONE_DIR}/client/include")
endif()
foreach(lodestoneTarget IN ITEMS lodestone lodestone_client_library lodestone_client_header_check lodestone_json lodestone_suggest)
    if (TARGET ${lodestoneTarget})
        set_target_properties(${lodestoneTarget} PROPERTIES FOLDER "third_party/lodestone")
    endif()
endforeach()
