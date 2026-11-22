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
if (EMSCRIPTEN)
    # The cooker is a host tool: use an existing host build of it
    set(LODESTONE_BUILD_COOKER OFF CACHE BOOL "The cooker does not build for Emscripten" FORCE)
    if (NOT LODESTONE_COOKER_EXECUTABLE)
        set(diamondDogsCookerCandidates "")
        foreach(presetDir IN ITEMS ninja-clang-cl ninja-msvc)
            foreach(config IN ITEMS RelWithDebInfo Release Debug)
                list(APPEND diamondDogsCookerCandidates
                    "${DIAMOND_DOGS_LODESTONE_DIR}/build/${presetDir}/tools/cooker_console/${config}/lodestone_cooker_console.exe")
            endforeach()
        endforeach()
        foreach(presetDir IN ITEMS ninja-msvc ninja-clang-cl)
            foreach(config IN ITEMS RelWithDebInfo Debug Release)
                list(APPEND diamondDogsCookerCandidates
                    "${CMAKE_SOURCE_DIR}/build/${presetDir}/third_party/lodestone/tools/cooker_console/${config}/lodestone_cooker_console.exe")
            endforeach()
        endforeach()
        unset(diamondDogsCookerFound)
        foreach(candidate IN LISTS diamondDogsCookerCandidates)
            if (EXISTS "${candidate}")
                execute_process(
                    COMMAND "${candidate}" --help
                    OUTPUT_VARIABLE diamondDogsCookerHelp
                    ERROR_VARIABLE diamondDogsCookerHelp)
                if (diamondDogsCookerHelp MATCHES "--host-types")
                    set(diamondDogsCookerFound "${candidate}")
                    break()
                endif()
            endif()
        endforeach()
        if (NOT diamondDogsCookerFound)
            message(FATAL_ERROR "No current lodestone_cooker_console.exe with --host-types found. Rebuild lodestone_cooker_console in ninja-msvc, or set LODESTONE_COOKER_EXECUTABLE.")
        endif()
        set(LODESTONE_COOKER_EXECUTABLE "${diamondDogsCookerFound}" CACHE FILEPATH "Host Lodestone cooker executable")
    endif()
else()
    set(LODESTONE_BUILD_COOKER ON CACHE BOOL "DiamondDogs cooks its shaders as a build step")
endif()

# Fixed TLS must stay off. Several mimalloc instances share the cooker process: Lodestone builds Slang in its own
# tree, and slang-compiler.dll keeps a private mimalloc, while the executable links ours. With fixed TLS, all of them
# claim the same TLS slot. slang-bootstrap crashed with an access violation while generating the core module when
# this was on (measured 2026-10-19).
set(MI_WIN_USE_FIXED_TLS OFF CACHE BOOL "Must stay off: several mimalloc instances share a process" FORCE)

add_subdirectory("${DIAMOND_DOGS_LODESTONE_DIR}" "${CMAKE_BINARY_DIR}/third_party/lodestone")

if (NOT TARGET xxHash::xxhash)
    add_library(xxhash STATIC "${DIAMOND_DOGS_LODESTONE_DIR}/third_party/xxHash/xxhash.c")
    target_include_directories(xxhash PUBLIC "${DIAMOND_DOGS_LODESTONE_DIR}/third_party/xxHash")
    add_library(xxHash::xxhash ALIAS xxhash)
endif()

if (TARGET lodestone_cooker_console)
    set_target_properties(lodestone_cooker_console PROPERTIES FOLDER "third_party/lodestone")
endif()
foreach(lodestoneTarget IN ITEMS lodestone lodestone_client_library lodestone_client_header_check lodestone_json lodestone_suggest)
    if (TARGET ${lodestoneTarget})
        set_target_properties(${lodestoneTarget} PROPERTIES FOLDER "third_party/lodestone")
    endif()
endforeach()

# Lodestone adds its clang Debug flags when the compiler ID is not MSVC, and clang-cl reports Clang. clang-cl
# ignores -fno-omit-frame-pointer with a warning on every file. /Od keeps frame pointers anyway, so we drop it.
# Request E4 in Lodestone's docs/diamonddogs-requests.md
if (NOT EMSCRIPTEN AND NOT DIAMOND_DOGS_COMPILER_IS_CL AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    foreach(lodestoneTarget IN ITEMS lodestone lodestone_client_library)
        if (TARGET ${lodestoneTarget})
            get_target_property(lodestoneOptions ${lodestoneTarget} COMPILE_OPTIONS)

            if (lodestoneOptions)
                string(REPLACE ";-fno-omit-frame-pointer" "" lodestoneOptions "${lodestoneOptions}")
                set_target_properties(${lodestoneTarget} PROPERTIES COMPILE_OPTIONS "${lodestoneOptions}")
            endif()
        endif()
    endforeach()
endif()

# DiamondDogs reads Lodestone's copy of xxHash on purpose: one checkout of the library, and one version, for the
# cooker and for us. The cooker always builds here, and its add_xxhash() defines this target.
if (NOT TARGET xxHash::xxhash)
    message(FATAL_ERROR
        "Lodestone did not define xxHash::xxhash. DiamondDogs uses Lodestone's xxHash "
        "(${DIAMOND_DOGS_LODESTONE_DIR}/third_party/xxHash), which add_xxhash() in cmake/LodestoneCooker.cmake adds.")
endif()
