# The OceanFft demo: the ocean FFT compute chain (assets/shaders/compute/Ocean) and its render passes
# (assets/shaders/shading/ocean), cooked by Lodestone into one bundle as a build step.
# ocean_simulation is the compute side on its own, so the headless OceanFftTest runs the same code as the demo.
set(OCEAN_FFT_SHADER_DIR "${PROJECT_SOURCE_DIR}/assets/shaders/compute/Ocean")

# ocean_fft_support.py turns OceanFftConfig.yaml into the spreading-normalization fits and LUTs. The LUTs are C++ sources
# in the build directory, and so is OceanFftGenerated.slang, which every cook of Spectra.slang finds through INCLUDE_DIRS.
# Needs numpy and pyyaml.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -c "import numpy, yaml"
    RESULT_VARIABLE OCEAN_FFT_PYTHON_MODULES_MISSING
    OUTPUT_QUIET ERROR_QUIET)
if (OCEAN_FFT_PYTHON_MODULES_MISSING)
    message(FATAL_ERROR "OceanFft: ${Python3_EXECUTABLE} needs numpy and pyyaml for assets/scripts/ocean_fft_support.py "
                        "(\"${Python3_EXECUTABLE}\" -m pip install --user numpy pyyaml)")
endif()
set(OCEAN_FFT_SCRIPT_DIR "${PROJECT_SOURCE_DIR}/assets/scripts")
set(OCEAN_FFT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(OCEAN_FFT_GENERATED_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/shaders/Ocean"
    CACHE INTERNAL "The folder of OceanFftGenerated.slang, which is written at build time")
set(OCEAN_FFT_GENERATED_SOURCES
    "${OCEAN_FFT_GENERATED_DIR}/DonelanBannerNormLUT.cpp"
    "${OCEAN_FFT_GENERATED_DIR}/DonelanBannerNormLUT.hpp"
    "${OCEAN_FFT_GENERATED_DIR}/SinCosLUT.cpp"
    "${OCEAN_FFT_GENERATED_DIR}/SinCosLUT.hpp")
set(OCEAN_FFT_SUPPORT_STAMP "${OCEAN_FFT_GENERATED_DIR}/ocean_fft_support.stamp")
add_custom_command(
    OUTPUT "${OCEAN_FFT_SUPPORT_STAMP}"
    BYPRODUCTS ${OCEAN_FFT_GENERATED_SOURCES} "${OCEAN_FFT_GENERATED_SHADER_DIR}/OceanFftGenerated.slang"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OCEAN_FFT_GENERATED_SHADER_DIR}"
    COMMAND Python3::Interpreter "${OCEAN_FFT_SCRIPT_DIR}/ocean_fft_support.py"
        --config "${OCEAN_FFT_SCRIPT_DIR}/OceanFftConfig.yaml"
        --lut_dir "${OCEAN_FFT_GENERATED_DIR}"
        --shader_dir "${OCEAN_FFT_GENERATED_SHADER_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${OCEAN_FFT_SUPPORT_STAMP}"
    DEPENDS "${OCEAN_FFT_SCRIPT_DIR}/ocean_fft_support.py" "${OCEAN_FFT_SCRIPT_DIR}/OceanFftConfig.yaml"
    COMMENT "Generating the OceanFft spreading fits and LUTs"
    VERBATIM)
add_custom_target(ocean_fft_support_data DEPENDS "${OCEAN_FFT_SUPPORT_STAMP}")
set_target_properties(ocean_fft_support_data PROPERTIES FOLDER "Applications/OceanFft")

# texture_pack.py packs Foam002 and WetSand (Ground095A) materials into KTX textures and JSON descriptors.
# It needs Pillow and numpy, and the Python above may not have Pillow, so this one is its own variable.
set(OCEAN_FFT_TEXTURE_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH
    "Python with Pillow and numpy, which runs assets/scripts/texture_pack.py")
execute_process(
    COMMAND "${OCEAN_FFT_TEXTURE_PYTHON}" -c "import PIL, numpy"
    RESULT_VARIABLE OCEAN_FFT_TEXTURE_PYTHON_MODULES_MISSING
    OUTPUT_QUIET ERROR_QUIET)
if (OCEAN_FFT_TEXTURE_PYTHON_MODULES_MISSING)
    message(FATAL_ERROR "OceanFft: OCEAN_FFT_TEXTURE_PYTHON (${OCEAN_FFT_TEXTURE_PYTHON}) needs Pillow and numpy for "
                        "assets/scripts/texture_pack.py. Install them, or set the variable to a Python that has them, "
                        "for example -DOCEAN_FFT_TEXTURE_PYTHON=C:/Python313/python.exe")
endif()

set(OCEAN_FFT_FOAM_MATERIAL_NAME "Foam002")
set(OCEAN_FFT_FOAM_SOURCE_DIR "${PROJECT_SOURCE_DIR}/assets/textures/${OCEAN_FFT_FOAM_MATERIAL_NAME}")
set(OCEAN_FFT_FOAM_DIR "${CMAKE_CURRENT_BINARY_DIR}/foam")
set(OCEAN_FFT_FOAM_FILES
    "${OCEAN_FFT_FOAM_DIR}/${OCEAN_FFT_FOAM_MATERIAL_NAME}_Surface.ktx"
    "${OCEAN_FFT_FOAM_DIR}/${OCEAN_FFT_FOAM_MATERIAL_NAME}_Normal.ktx"
    "${OCEAN_FFT_FOAM_DIR}/${OCEAN_FFT_FOAM_MATERIAL_NAME}_Material.json")
add_custom_command(
    OUTPUT ${OCEAN_FFT_FOAM_FILES}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OCEAN_FFT_FOAM_DIR}"
    COMMAND "${OCEAN_FFT_TEXTURE_PYTHON}" "${OCEAN_FFT_SCRIPT_DIR}/texture_pack.py"
        --mode monochrome "${OCEAN_FFT_FOAM_SOURCE_DIR}" "${OCEAN_FFT_FOAM_MATERIAL_NAME}" "${OCEAN_FFT_FOAM_DIR}" 1024
    DEPENDS "${OCEAN_FFT_SCRIPT_DIR}/texture_pack.py"
        "${OCEAN_FFT_FOAM_SOURCE_DIR}/${OCEAN_FFT_FOAM_MATERIAL_NAME}_4K_Color.jpg"
        "${OCEAN_FFT_FOAM_SOURCE_DIR}/${OCEAN_FFT_FOAM_MATERIAL_NAME}_4K_Opacity.jpg"
        "${OCEAN_FFT_FOAM_SOURCE_DIR}/${OCEAN_FFT_FOAM_MATERIAL_NAME}_4K_Displacement.jpg"
        "${OCEAN_FFT_FOAM_SOURCE_DIR}/${OCEAN_FFT_FOAM_MATERIAL_NAME}_4K_Roughness.jpg"
        "${OCEAN_FFT_FOAM_SOURCE_DIR}/${OCEAN_FFT_FOAM_MATERIAL_NAME}_4K_NormalGL.jpg"
    COMMENT "Packing the ${OCEAN_FFT_FOAM_MATERIAL_NAME} foam textures"
    VERBATIM)
add_custom_target(ocean_fft_foam_textures DEPENDS ${OCEAN_FFT_FOAM_FILES})
set_target_properties(ocean_fft_foam_textures PROPERTIES FOLDER "Applications/OceanFft")

set(OCEAN_FFT_SEABED_MATERIAL_NAME "Ground095A")
set(OCEAN_FFT_SEABED_SOURCE_DIR "${PROJECT_SOURCE_DIR}/assets/textures/WetSand")
set(OCEAN_FFT_SEABED_DIR "${CMAKE_CURRENT_BINARY_DIR}/seabed")
set(OCEAN_FFT_SEABED_FILES
    "${OCEAN_FFT_SEABED_DIR}/${OCEAN_FFT_SEABED_MATERIAL_NAME}_Albedo.ktx"
    "${OCEAN_FFT_SEABED_DIR}/${OCEAN_FFT_SEABED_MATERIAL_NAME}_Ord.ktx"
    "${OCEAN_FFT_SEABED_DIR}/${OCEAN_FFT_SEABED_MATERIAL_NAME}_Normal.ktx"
    "${OCEAN_FFT_SEABED_DIR}/${OCEAN_FFT_SEABED_MATERIAL_NAME}_Material.json")
add_custom_command(
    OUTPUT ${OCEAN_FFT_SEABED_FILES}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OCEAN_FFT_SEABED_DIR}"
    COMMAND "${OCEAN_FFT_TEXTURE_PYTHON}" "${OCEAN_FFT_SCRIPT_DIR}/texture_pack.py"
        --mode ord "${OCEAN_FFT_SEABED_SOURCE_DIR}" "${OCEAN_FFT_SEABED_MATERIAL_NAME}" "${OCEAN_FFT_SEABED_DIR}" 2048
    DEPENDS "${OCEAN_FFT_SCRIPT_DIR}/texture_pack.py"
        "${OCEAN_FFT_SEABED_SOURCE_DIR}/${OCEAN_FFT_SEABED_MATERIAL_NAME}_4K-PNG_Color.png"
        "${OCEAN_FFT_SEABED_SOURCE_DIR}/${OCEAN_FFT_SEABED_MATERIAL_NAME}_4K-PNG_AmbientOcclusion.png"
        "${OCEAN_FFT_SEABED_SOURCE_DIR}/${OCEAN_FFT_SEABED_MATERIAL_NAME}_4K-PNG_Roughness.png"
        "${OCEAN_FFT_SEABED_SOURCE_DIR}/${OCEAN_FFT_SEABED_MATERIAL_NAME}_4K-PNG_Displacement.png"
        "${OCEAN_FFT_SEABED_SOURCE_DIR}/${OCEAN_FFT_SEABED_MATERIAL_NAME}_4K-PNG_NormalGL.png"
    COMMENT "Packing the ${OCEAN_FFT_SEABED_MATERIAL_NAME} seabed textures"
    VERBATIM)
add_custom_target(ocean_fft_seabed_textures DEPENDS ${OCEAN_FFT_SEABED_FILES})
set_target_properties(ocean_fft_seabed_textures PROPERTIES FOLDER "Applications/OceanFft")

lodestone_add_shader_cook(ocean_fft_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    POLICY_FILE "${OCEAN_FFT_SHADER_DIR}/OceanFftPolicy.toml"
    INCLUDE_DIRS "${OCEAN_FFT_GENERATED_SHADER_DIR}"
                 "${PROJECT_SOURCE_DIR}/assets/shaders/common/Color"
                 "${CMAKE_BINARY_DIR}/generated/shaders/Color"
                 "${PROJECT_SOURCE_DIR}/assets/shaders/shading/ocean"
    OPTIONS --O3 --g3
    MODULES "${OCEAN_FFT_SHADER_DIR}/OceanFft.slang"
            "${PROJECT_SOURCE_DIR}/assets/shaders/shading/ocean/OceanRender.slang"
            "${PROJECT_SOURCE_DIR}/assets/shaders/compute/Quadtree/Quadtree.slang"
            "${PROJECT_SOURCE_DIR}/assets/shaders/shading/ocean/OceanEnvironment.slang"
            "${PROJECT_SOURCE_DIR}/assets/shaders/shading/ocean/OceanAtmosphere.slang"
            "${PROJECT_SOURCE_DIR}/assets/shaders/shading/ocean/OceanDeferred.slang"
            "${GPU_PYRAMID_SHADER_MODULE}")
set_target_properties(ocean_fft_shaders PROPERTIES FOLDER "Applications/OceanFft")
# the cook reads OceanFftGenerated.slang and ddColorGenerated.slang, so the generators run first
add_dependencies(ocean_fft_shaders ocean_fft_support_data color_pipe_shader_data)

add_library(ocean_simulation STATIC
    "simulation/OceanSimulation.cpp"
    "simulation/OceanSimulation.hpp"
    "simulation/SeaState.cpp"
    "simulation/SeaState.hpp"
    ${OCEAN_FFT_GENERATED_SOURCES})
target_include_directories(ocean_simulation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/simulation")
target_include_directories(ocean_simulation PRIVATE "${OCEAN_FFT_GENERATED_DIR}")
target_link_libraries(ocean_simulation PUBLIC gpu_pyramid rhi_resources rhi_system foundation lodestone::client_library)
target_compile_definitions(ocean_simulation PUBLIC
    "NOMINMAX"
    "OCEAN_FFT_MANIFEST=\"$<TARGET_PROPERTY:ocean_fft_shaders,LODESTONE_MANIFEST>\"")
add_dependencies(ocean_simulation ocean_fft_shaders)
set_target_properties(ocean_simulation PROPERTIES FOLDER "Applications/OceanFft" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(ocean_simulation LIBRARY)

# ocean_foam is the persistent foam: the clipmap, its compute step, and the replay that rebuilds it. It is a library on its
# own so that a test can link it without the demo.
add_library(ocean_foam STATIC
    "foam/OceanFoam.cpp"
    "foam/OceanFoam.hpp"
    "foam/FoamMaterial.cpp"
    "foam/FoamMaterial.hpp"
    "foam/FoamReplay.cpp"
    "foam/FoamReplay.hpp")
target_include_directories(ocean_foam PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/foam")
target_include_directories(ocean_foam PRIVATE "${PROJECT_SOURCE_DIR}/third_party/json/include")
target_link_libraries(ocean_foam PUBLIC ocean_simulation PRIVATE gli)
set_target_properties(ocean_foam PROPERTIES FOLDER "Applications/OceanFft" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(ocean_foam LIBRARY)

# The demo itself. It borrows the VulkanScene frame loop from the integration test fixtures until there is an
# application framework to put it in.
set(DIAMOND_DOGS_TEST_FIXTURES "${PROJECT_SOURCE_DIR}/tests/integration_tests/fixtures")
add_executable(OceanFft
    "main.cpp"
    "ArcballCamera.cpp"
    "ArcballCamera.hpp"
    "OceanScene.cpp"
    "OceanScene.hpp"
    "OceanUi.cpp"
    "OceanUi.hpp"
    "SceneCommands.cpp"
    "SceneCommands.hpp"
    "SeabedMaterial.cpp"
    "SeabedMaterial.hpp"
    "ShaderObjectState.cpp"
    "ShaderObjectState.hpp"
    "mesh/OceanCdlod.cpp"
    "mesh/OceanCdlod.hpp"
    "environment/ExrLoader.cpp"
    "environment/ExrLoader.hpp"
    "environment/OceanEnvironment.cpp"
    "environment/OceanEnvironment.hpp"
    "environment/OceanAtmosphere.cpp"
    "environment/OceanAtmosphere.hpp"
    "${DIAMOND_DOGS_TEST_FIXTURES}/VulkanScene.cpp"
    "${DIAMOND_DOGS_TEST_FIXTURES}/VulkanScene.hpp"
    "${DIAMOND_DOGS_TEST_FIXTURES}/FrameLimit.cpp"
    "${DIAMOND_DOGS_TEST_FIXTURES}/FrameLimit.hpp")
# platform_system does not export its include folder (ADD_INTEGRATION_TEST adds it by hand, too)
target_include_directories(OceanFft PRIVATE
    "${DIAMOND_DOGS_TEST_FIXTURES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/mesh"
    "${CMAKE_CURRENT_SOURCE_DIR}/environment"
    "${PROJECT_SOURCE_DIR}/modules/ColorPipe/include"
    "${PROJECT_SOURCE_DIR}/modules/PlatformSystem/include"
    "${CMAKE_BINARY_DIR}/include"
    "${PROJECT_SOURCE_DIR}/third_party/json/include"
    "${PROJECT_SOURCE_DIR}/third_party/basis_universal/encoder"
    "${PROJECT_SOURCE_DIR}/third_party/basis_universal/encoder/3rdparty")
target_compile_definitions(OceanFft PRIVATE
    "OCEAN_FFT_FONT_DIR=\"${PROJECT_SOURCE_DIR}/assets/fonts\""
    "OCEAN_FFT_FOAM_DIR=\"${OCEAN_FFT_FOAM_DIR}\""
    "OCEAN_FFT_FOAM_NAME=\"${OCEAN_FFT_FOAM_MATERIAL_NAME}\""
    "OCEAN_FFT_SEABED_DIR=\"${OCEAN_FFT_SEABED_DIR}\""
    "OCEAN_FFT_SEABED_NAME=\"${OCEAN_FFT_SEABED_MATERIAL_NAME}\""
    "OCEAN_FFT_COLOR_PIPE_MANIFEST=\"$<TARGET_PROPERTY:color_pipe_shaders,LODESTONE_MANIFEST>\"")
target_link_libraries(OceanFft PRIVATE ocean_simulation ocean_foam color_pipe frame_capture imgui_module platform_system glfw gli ${Vulkan_LIBRARY})
set_target_properties(OceanFft PROPERTIES FOLDER "Applications/OceanFft" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(OceanFft LIBRARY)
if (DIAMOND_DOGS_COMPILER_IS_CL)
    target_compile_options(OceanFft PRIVATE "/MP")
endif()

# OceanFftBenchmark: GPU time per compute pass, for every IFFT size, radix and wave-op setting. Headless and
# unvalidated; it measures only where GPU timers are compiled in (Debug, RelWithDebInfo)
add_executable(OceanFftBenchmark "benchmark/main.cpp")
target_link_libraries(OceanFftBenchmark PRIVATE ocean_simulation rhi_resources)
set_target_properties(OceanFftBenchmark PROPERTIES FOLDER "Applications/OceanFft" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(OceanFftBenchmark LIBRARY)

# The demo looks for its shader bundle next to the executable first (main.cpp), so the output folder runs on its own.
# The copy is its own step, which depends on the bundle: a POST_BUILD copy ran only when the executable relinked, so
# a change to the shaders alone left the old bundle next to it
set(OCEAN_FFT_BUNDLE "$<TARGET_PROPERTY:ocean_fft_shaders,LODESTONE_MANIFEST>")
set(OCEAN_FFT_BUNDLE_COPY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/OceanFft.ldmanifest")
set(OCEAN_FFT_COLOR_PIPE_BUNDLE "$<TARGET_PROPERTY:color_pipe_shaders,LODESTONE_MANIFEST>")
set(OCEAN_FFT_COLOR_PIPE_BUNDLE_COPY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/ColorPipe.ldmanifest")
add_custom_command(
    OUTPUT "${OCEAN_FFT_BUNDLE_COPY}" "${OCEAN_FFT_COLOR_PIPE_BUNDLE_COPY}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OCEAN_FFT_BUNDLE}" "${OCEAN_FFT_BUNDLE_COPY}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OCEAN_FFT_COLOR_PIPE_BUNDLE}" "${OCEAN_FFT_COLOR_PIPE_BUNDLE_COPY}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/assets/fonts" "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${OCEAN_FFT_FOAM_FILES} "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${OCEAN_FFT_SEABED_FILES} "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    DEPENDS "${OCEAN_FFT_BUNDLE}" "${OCEAN_FFT_COLOR_PIPE_BUNDLE}" ocean_fft_shaders color_pipe_shaders ${OCEAN_FFT_FOAM_FILES} ${OCEAN_FFT_SEABED_FILES}
    COMMENT "Copying the OceanFft shader bundle, ColorPipe bundle, fonts, foam and seabed textures next to the executable"
    VERBATIM)
add_custom_target(ocean_fft_bundle_copy DEPENDS "${OCEAN_FFT_BUNDLE_COPY}" "${OCEAN_FFT_COLOR_PIPE_BUNDLE_COPY}")
set_target_properties(ocean_fft_bundle_copy PROPERTIES FOLDER "Applications/OceanFft")
add_dependencies(OceanFft ocean_fft_bundle_copy)

# OceanFft_package: a folder to copy to another machine, in build/<preset>/package/OceanFft-<config>. It holds the
# executable, the shader bundle, the foam textures, the seabed textures and, with a DLL C runtime, the release VC++ runtime DLLs (app-local deployment,
# so the other machine needs no redistributable installer). Use a Release build: Debug executables need the debug
# runtime, which only Visual Studio installs. The other machine needs only a GPU driver with Vulkan and
# VK_EXT_shader_object; without the Vulkan SDK the demo runs without validation.
set(OCEAN_FFT_PACKAGE_DIR "${CMAKE_BINARY_DIR}/package/OceanFft-$<CONFIG>")
set(OCEAN_FFT_PACKAGE_RUNTIME_DLLS "")

if (MSVC AND (NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY OR CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL"))
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    set(OCEAN_FFT_PACKAGE_RUNTIME_DLLS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
endif()

add_custom_target(OceanFft_package
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${OCEAN_FFT_PACKAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OCEAN_FFT_PACKAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:OceanFft>" ${OCEAN_FFT_PACKAGE_RUNTIME_DLLS} "${OCEAN_FFT_PACKAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${OCEAN_FFT_BUNDLE}" "${OCEAN_FFT_PACKAGE_DIR}/OceanFft.ldmanifest"
    COMMAND "${CMAKE_COMMAND}" -E copy "${OCEAN_FFT_COLOR_PIPE_BUNDLE}" "${OCEAN_FFT_PACKAGE_DIR}/ColorPipe.ldmanifest"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/assets/fonts" "${OCEAN_FFT_PACKAGE_DIR}/fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy ${OCEAN_FFT_FOAM_FILES} "${OCEAN_FFT_PACKAGE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy ${OCEAN_FFT_SEABED_FILES} "${OCEAN_FFT_PACKAGE_DIR}"
    COMMENT "Packaging OceanFft into ${OCEAN_FFT_PACKAGE_DIR}"
    VERBATIM)
add_dependencies(OceanFft_package OceanFft ocean_fft_foam_textures ocean_fft_seabed_textures color_pipe_shaders)
set_target_properties(OceanFft_package PROPERTIES FOLDER "Applications/OceanFft")
