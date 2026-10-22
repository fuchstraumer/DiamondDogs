# The OceanFft demo: the ocean FFT compute chain (assets/shaders/compute/Ocean) and its render passes
# (assets/shaders/shading/ocean), cooked by Lodestone into one bundle as a build step.
# ocean_simulation is the compute side on its own, so the headless OceanFftTest runs the same code as the demo.
set(OCEAN_FFT_SHADER_DIR "${PROJECT_SOURCE_DIR}/assets/shaders/compute/Ocean")

# ocean_fft_support.py turns OceanFftConfig.yaml into the spreading-normalization fits and LUTs. The LUTs are C++ sources
# in the build directory. OceanFftGenerated.slang has to sit next to Spectra.slang, because the cooker only searches a
# module's own folder, so it is written into the source tree (only when its contents change). Needs numpy and pyyaml.
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
set(OCEAN_FFT_GENERATED_SOURCES
    "${OCEAN_FFT_GENERATED_DIR}/DonelanBannerNormLUT.cpp"
    "${OCEAN_FFT_GENERATED_DIR}/DonelanBannerNormLUT.hpp"
    "${OCEAN_FFT_GENERATED_DIR}/SinCosLUT.cpp"
    "${OCEAN_FFT_GENERATED_DIR}/SinCosLUT.hpp")
set(OCEAN_FFT_SUPPORT_STAMP "${OCEAN_FFT_GENERATED_DIR}/ocean_fft_support.stamp")
add_custom_command(
    OUTPUT "${OCEAN_FFT_SUPPORT_STAMP}"
    BYPRODUCTS ${OCEAN_FFT_GENERATED_SOURCES} "${OCEAN_FFT_SHADER_DIR}/OceanFftGenerated.slang"
    COMMAND Python3::Interpreter "${OCEAN_FFT_SCRIPT_DIR}/ocean_fft_support.py"
        --config "${OCEAN_FFT_SCRIPT_DIR}/OceanFftConfig.yaml"
        --lut_dir "${OCEAN_FFT_GENERATED_DIR}"
        --shader_dir "${OCEAN_FFT_SHADER_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${OCEAN_FFT_SUPPORT_STAMP}"
    DEPENDS "${OCEAN_FFT_SCRIPT_DIR}/ocean_fft_support.py" "${OCEAN_FFT_SCRIPT_DIR}/OceanFftConfig.yaml"
    COMMENT "Generating the OceanFft spreading fits and LUTs"
    VERBATIM)
add_custom_target(ocean_fft_support_data DEPENDS "${OCEAN_FFT_SUPPORT_STAMP}")
set_target_properties(ocean_fft_support_data PROPERTIES FOLDER "Applications/OceanFft")

lodestone_add_shader_cook(ocean_fft_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    POLICY_FILE "${OCEAN_FFT_SHADER_DIR}/OceanFftPolicy.toml"
    MODULES "${OCEAN_FFT_SHADER_DIR}/OceanFft.slang" "${PROJECT_SOURCE_DIR}/assets/shaders/shading/ocean/OceanRender.slang")
set_target_properties(ocean_fft_shaders PROPERTIES FOLDER "Applications/OceanFft")
# the cook reads OceanFftGenerated.slang, so the generator runs first
add_dependencies(ocean_fft_shaders ocean_fft_support_data)

add_library(ocean_simulation STATIC
    "simulation/OceanSimulation.cpp"
    "simulation/OceanSimulation.hpp"
    "simulation/ManifestDescriptors.cpp"
    "simulation/ManifestDescriptors.hpp"
    "simulation/SeaState.cpp"
    "simulation/SeaState.hpp"
    ${OCEAN_FFT_GENERATED_SOURCES})
target_include_directories(ocean_simulation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/simulation")
target_include_directories(ocean_simulation PRIVATE "${OCEAN_FFT_GENERATED_DIR}")
target_link_libraries(ocean_simulation PUBLIC rhi_resources rhi_system foundation lodestone::client_library)
target_compile_definitions(ocean_simulation PUBLIC
    "NOMINMAX"
    "OCEAN_FFT_MANIFEST=\"$<TARGET_PROPERTY:ocean_fft_shaders,LODESTONE_MANIFEST>\"")
add_dependencies(ocean_simulation ocean_fft_shaders)
set_target_properties(ocean_simulation PROPERTIES FOLDER "Applications/OceanFft" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)

# The demo itself. It borrows the VulkanScene frame loop from the integration test fixtures until there is an
# application framework to put it in.
set(DIAMOND_DOGS_TEST_FIXTURES "${PROJECT_SOURCE_DIR}/tests/integration_tests/fixtures")
add_executable(OceanFft
    "main.cpp"
    "ArcballCamera.cpp"
    "ArcballCamera.hpp"
    "OceanScene.cpp"
    "OceanScene.hpp"
    "SceneCommands.cpp"
    "SceneCommands.hpp"
    "ShaderObjectState.cpp"
    "ShaderObjectState.hpp"
    "${DIAMOND_DOGS_TEST_FIXTURES}/VulkanScene.cpp"
    "${DIAMOND_DOGS_TEST_FIXTURES}/VulkanScene.hpp"
    "${DIAMOND_DOGS_TEST_FIXTURES}/FrameLimit.cpp"
    "${DIAMOND_DOGS_TEST_FIXTURES}/FrameLimit.hpp")
# platform_system does not export its include folder (ADD_INTEGRATION_TEST adds it by hand, too)
target_include_directories(OceanFft PRIVATE "${DIAMOND_DOGS_TEST_FIXTURES}" "${PROJECT_SOURCE_DIR}/modules/PlatformSystem/include" "${CMAKE_BINARY_DIR}/include")
target_link_libraries(OceanFft PRIVATE ocean_simulation frame_capture imgui_module platform_system glfw ${Vulkan_LIBRARY})
set_target_properties(OceanFft PROPERTIES FOLDER "Applications/OceanFft" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
if (DIAMOND_DOGS_COMPILER_IS_CL)
    target_compile_options(OceanFft PRIVATE "/MP")
endif()

# OceanFftBenchmark: GPU time per compute pass, for every IFFT size, radix and wave-op setting. Headless and
# unvalidated; it measures only where GPU timers are compiled in (Debug, RelWithDebInfo)
add_executable(OceanFftBenchmark "benchmark/main.cpp")
target_link_libraries(OceanFftBenchmark PRIVATE ocean_simulation rhi_resources)
set_target_properties(OceanFftBenchmark PROPERTIES FOLDER "Applications/OceanFft" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)

# The demo looks for its shader bundle next to the executable first (main.cpp), so the output folder runs on its own
set(OCEAN_FFT_BUNDLE "$<TARGET_PROPERTY:ocean_fft_shaders,LODESTONE_MANIFEST>")
add_custom_command(TARGET OceanFft POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OCEAN_FFT_BUNDLE}" "$<TARGET_FILE_DIR:OceanFft>/OceanFft.ldmanifest"
    COMMENT "Copying the OceanFft shader bundle next to the executable"
    VERBATIM)

# OceanFft_package: a folder to copy to another machine, in build/<preset>/package/OceanFft-<config>. It holds the
# executable and the shader bundle and, with a DLL C runtime, the release VC++ runtime DLLs (app-local deployment,
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
    COMMENT "Packaging OceanFft into ${OCEAN_FFT_PACKAGE_DIR}"
    VERBATIM)
add_dependencies(OceanFft_package OceanFft)
set_target_properties(OceanFft_package PROPERTIES FOLDER "Applications/OceanFft")
