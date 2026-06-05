# Configure Slang compiler with minimal dependencies for Vulkan/SPIR-V compilation
# This module disables unnecessary Slang features and organizes targets into IDE folders

# Disable Slang features not needed for Vulkan/SPIR-V compilation API
set(SLANG_ENABLE_DXIL OFF CACHE BOOL "Enable generating DXIL using DXC")
set(SLANG_ENABLE_CUDA OFF CACHE BOOL "Enable CUDA tests using CUDA found in CUDA_PATH")
set(SLANG_ENABLE_OPTIX OFF CACHE BOOL "Enable OptiX build/tests, requires SLANG_ENABLE_CUDA")
set(SLANG_ENABLE_NVAPI OFF CACHE BOOL "Enable NVAPI usage (Only available for builds targeting Windows)")
set(SLANG_ENABLE_AFTERMATH OFF CACHE BOOL "Enable Aftermath in GFX, and add aftermath crash example to project")
set(SLANG_ENABLE_GFX OFF CACHE BOOL "Enable gfx targets")
set(SLANG_ENABLE_SLANGD OFF CACHE BOOL "Enable language server target")
set(SLANG_ENABLE_SLANGC OFF CACHE BOOL "Enable standalone compiler target")
set(SLANG_ENABLE_SLANGI OFF CACHE BOOL "Enable Slang interpreter target")
set(SLANG_ENABLE_TESTS OFF CACHE BOOL "Enable test targets, some tests may require SLANG_ENABLE_GFX, SLANG_ENABLE_SLANGD or SLANG_ENABLE_SLANGRT")
set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "Enable example targets, requires SLANG_ENABLE_GFX")
set(SLANG_ENABLE_REPLAYER OFF CACHE BOOL "Enable slang-replay tool")
set(SLANG_ENABLE_SLANG_RHI OFF CACHE BOOL "Use slang-rhi as dependency")
set(SLANG_EXCLUDE_DAWN ON CACHE BOOL "Optionally exclude webgpu_dawn from the build")
set(SLANG_EXCLUDE_TINT ON CACHE BOOL "Optionally exclude slang-tint from the build")

# Disable Slang's mimalloc for SPIRV-Tools to avoid duplicate target conflicts
# DiamondDogs uses its own mimalloc from third_party/mimalloc
set(SLANG_ENABLE_SPIRV_TOOLS_MIMALLOC OFF CACHE BOOL "Disable mimalloc for SPIRV-Tools to avoid conflicts")

# Use Slang's bundled Vulkan headers instead of system headers to avoid path issues
set(SLANG_USE_SYSTEM_VULKAN_HEADERS OFF CACHE BOOL "Use Slang's bundled Vulkan headers")

# Function to organize all Slang targets into IDE folders
function(organize_slang_targets)
    # Enable folder support
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    
    # Main Slang targets
    if(TARGET slang)
        set_target_properties(slang PROPERTIES FOLDER "third_party/slang")
    endif()
    if(TARGET slang-rt)
        set_target_properties(slang-rt PROPERTIES FOLDER "third_party/slang")
    endif()
    if(TARGET core)
        set_target_properties(core PROPERTIES FOLDER "third_party/slang")
    endif()
    if(TARGET compiler-core)
        set_target_properties(compiler-core PROPERTIES FOLDER "third_party/slang")
    endif()
    if(TARGET slang-core-module)
        set_target_properties(slang-core-module PROPERTIES FOLDER "third_party/slang")
    endif()
    if(TARGET slang-common-objects)
        set_target_properties(slang-common-objects PROPERTIES FOLDER "third_party/slang")
    endif()

    # SPIRV dependencies
    if(TARGET SPIRV-Headers)
        set_target_properties(SPIRV-Headers PROPERTIES FOLDER "third_party/slang/external")
    endif()
    if(TARGET spirv-headers-example)
        set_target_properties(spirv-headers-example PROPERTIES FOLDER "third_party/slang/external")
    endif()

    # SPIRV-Tools targets
    if(TARGET spirv-tools-header)
        set_target_properties(spirv-tools-header PROPERTIES FOLDER "third_party/slang/external")
    endif()
    if(TARGET spirv-tools-core_tables)
        set_target_properties(spirv-tools-core_tables PROPERTIES FOLDER "third_party/slang/external")
    endif()
    if(TARGET spirv-tools-enum_string_mapping)
        set_target_properties(spirv-tools-enum_string_mapping PROPERTIES FOLDER "third_party/slang/external")
    endif()
    if(TARGET spirv-tools-extinst_tables)
        set_target_properties(spirv-tools-extinst_tables PROPERTIES FOLDER "third_party/slang/external")
    endif()
    if(TARGET SPIRV-Tools-static)
        set_target_properties(SPIRV-Tools-static PROPERTIES FOLDER "third_party/slang/external")
    endif()
    if(TARGET SPIRV-Tools-shared)
        set_target_properties(SPIRV-Tools-shared PROPERTIES FOLDER "third_party/slang/external")
    endif()

    # Compression libraries
    if(TARGET miniz)
        set_target_properties(miniz PROPERTIES FOLDER "third_party/slang/external")
    endif()
    if(TARGET lz4_static)
        set_target_properties(lz4_static PROPERTIES FOLDER "third_party/slang/external")
    endif()

    # Utilities
    if(TARGET unordered_dense)
        set_target_properties(unordered_dense PROPERTIES FOLDER "third_party/slang/external")
    endif()

    # Generated content targets
    if(TARGET slang-generate-headers)
        set_target_properties(slang-generate-headers PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-cpp-extractor)
        set_target_properties(slang-cpp-extractor PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-capability-generator)
        set_target_properties(slang-capability-generator PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET generators)
        set_target_properties(generators PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET all-generators)
        set_target_properties(all-generators PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET copy_slang_headers)
        set_target_properties(copy_slang_headers PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET copy-slang-llvm)
        set_target_properties(copy-slang-llvm PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET copy-slang-tint)
        set_target_properties(copy-slang-tint PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET copy-webgpu_dawn)
        set_target_properties(copy-webgpu_dawn PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET generate_core_module_headers)
        set_target_properties(generate_core_module_headers PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET prelude)
        set_target_properties(prelude PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-capability-defs)
        set_target_properties(slang-capability-defs PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-capability-lookup)
        set_target_properties(slang-capability-lookup PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-embedded-core-module)
        set_target_properties(slang-embedded-core-module PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-embedded-core-module-source)
        set_target_properties(slang-embedded-core-module-source PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-fiddle-output)
        set_target_properties(slang-fiddle-output PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-glsl-module)
        set_target_properties(slang-glsl-module PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-lookup-tables)
        set_target_properties(slang-lookup-tables PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-no-embedded-core-module)
        set_target_properties(slang-no-embedded-core-module PROPERTIES FOLDER "third_party/slang/generated")
    endif()
    if(TARGET slang-no-embedded-core-module-source)
        set_target_properties(slang-no-embedded-core-module-source PROPERTIES FOLDER "third_party/slang/generated")
    endif()

    # Generator tools
    if(TARGET slang-bootstrap)
        set_target_properties(slang-bootstrap PROPERTIES FOLDER "third_party/slang/generators")
    endif()
    if(TARGET slang-cpp-parser)
        set_target_properties(slang-cpp-parser PROPERTIES FOLDER "third_party/slang/generators")
    endif()
    if(TARGET slang-embed)
        set_target_properties(slang-embed PROPERTIES FOLDER "third_party/slang/generators")
    endif()
    if(TARGET slang-fiddle)
        set_target_properties(slang-fiddle PROPERTIES FOLDER "third_party/slang/generators")
    endif()
    if(TARGET slang-generate)
        set_target_properties(slang-generate PROPERTIES FOLDER "third_party/slang/generators")
    endif()
    if(TARGET slang-lookup-generator)
        set_target_properties(slang-lookup-generator PROPERTIES FOLDER "third_party/slang/generators")
    endif()
    if(TARGET slang-spirv-embed-generator)
        set_target_properties(slang-spirv-embed-generator PROPERTIES FOLDER "third_party/slang/generators")
    endif()
    if(TARGET slang-without-embedded-core-module)
        set_target_properties(slang-without-embedded-core-module PROPERTIES FOLDER "third_party/slang/generators")
    endif()

    # Prebuilt binaries
    if(TARGET copy-prebuilt-binaries)
        set_target_properties(copy-prebuilt-binaries PROPERTIES FOLDER "third_party/slang/external")
    endif()
    if(TARGET slang-llvm)
        set_target_properties(slang-llvm PROPERTIES FOLDER "third_party/slang/external")
    endif()
endfunction()

# Function to copy Slang runtime DLLs to target output directory
# This ensures executables can find slang.dll and its dependencies at runtime
function(copy_slang_dlls_to_target TARGET_NAME)
    if(NOT TARGET ${TARGET_NAME})
        message(WARNING "copy_slang_dlls_to_target: Target ${TARGET_NAME} does not exist")
        return()
    endif()

    if(NOT TARGET slang)
        message(WARNING "copy_slang_dlls_to_target: slang target does not exist")
        return()
    endif()

    # Define the list of DLLs that need to be copied
    # These are built by Slang and required at runtime
    set(SLANG_DLL_NAMES
        "slang.dll"
        "slang.pdb"
        "dxcompiler.dll"
        "dxil.dll"
        "d3dcompiler_47.dll"
    )

    # For each DLL, add a post-build command to copy it to the target's output directory
    foreach(DLL_NAME ${SLANG_DLL_NAMES})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_BINARY_DIR}/$<CONFIG>/bin/${DLL_NAME}"
                "$<TARGET_FILE_DIR:${TARGET_NAME}>/${DLL_NAME}"
            COMMENT "Copying ${DLL_NAME} to ${TARGET_NAME} output directory"
            VERBATIM
        )
    endforeach()
endfunction()
