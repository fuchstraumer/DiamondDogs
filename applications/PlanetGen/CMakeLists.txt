# PlanetGen: the C++ and Lodestone port of the PlanetLab plate model. The Python prototype and its reference files
# live in applications/PlanetLab, which git ignores. README.md explains the parts.
set(PLANET_GEN_SHADER_DIR "${PROJECT_SOURCE_DIR}/assets/shaders/compute/Planet")

lodestone_add_shader_cook(planet_gen_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    HOST_TYPES "math/ShaderHostTypes.hpp"
    TARGETS spirv
    OPTIONS --O3 --g3
    MODULES "${PLANET_GEN_SHADER_DIR}/PlanetLab.slang"
            "${PLANET_GEN_SHADER_DIR}/PlanetPresent.slang")
set_target_properties(planet_gen_shaders PROPERTIES FOLDER "Applications/PlanetGen")

# planet_gpu: the programs of the PlanetLab module with their reflected bindings, and named buffers
add_library(planet_gpu STATIC
    "gpu/GpuBuffers.cpp"
    "gpu/GpuBuffers.hpp"
    "gpu/Healpix.cpp"
    "gpu/Healpix.hpp"
    "gpu/PlanetGpuErrors.cpp"
    "gpu/PlanetGpuErrors.hpp"
    "gpu/PlanetSession.cpp"
    "gpu/PlanetSession.hpp"
    "gpu/PlanetShaders.cpp"
    "gpu/PlanetShaders.hpp"
    "gpu/PlanetStep.cpp"
    "gpu/PlanetStep.hpp")
target_include_directories(planet_gpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gpu")
target_link_libraries(planet_gpu PUBLIC bootstrap_vulkan rhi_resources rhi_system foundation lodestone::client_library)
target_compile_definitions(planet_gpu PUBLIC
    "NOMINMAX"
    "PLANET_GEN_MANIFEST=\"$<TARGET_PROPERTY:planet_gen_shaders,LODESTONE_MANIFEST>\"")
set_target_properties(planet_gpu PROPERTIES FOLDER "Applications/PlanetGen" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
add_dependencies(planet_gpu planet_gen_shaders)
diamond_dogs_add_pch(planet_gpu LIBRARY)

# planet_reference: the reference files of PlanetLab, and the buffers of one step made from them. The check and the
# bench both start from these
add_library(planet_reference STATIC
    "check/PlanetMath.cpp"
    "check/PlanetMath.hpp"
    "check/ReferenceSet.cpp"
    "check/ReferenceSet.hpp"
    "check/StepState.cpp"
    "check/StepState.hpp")
target_include_directories(planet_reference PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/check")
target_include_directories(planet_reference PRIVATE "${PROJECT_SOURCE_DIR}/third_party/json/include")
target_link_libraries(planet_reference PUBLIC planet_gpu)
set_target_properties(planet_reference PROPERTIES FOLDER "Applications/PlanetGen" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(planet_reference LIBRARY)

# PlanetGenCheck: each pass, and the whole step, against the reference files of PlanetLab. Headless, with Full validation
add_executable(PlanetGenCheck
    "check/CheckHarness.cpp"
    "check/CheckHarness.hpp"
    "check/StepChecks.hpp"
    "check/StepChecks.cpp"
    "check/SurfaceChecks.cpp"
    "check/main.cpp")
target_link_libraries(PlanetGenCheck PRIVATE planet_reference planet_gpu bootstrap_vulkan)
target_include_directories(PlanetGenCheck PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cooked")
set_target_properties(PlanetGenCheck PROPERTIES FOLDER "Applications/PlanetGen" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(PlanetGenCheck LIBRARY)

# PlanetGenBench: GPU time of each pass of the step, on the reference state. Headless, no validation. GPU timers exist
# in Debug and RelWithDebInfo only; measure in RelWithDebInfo
add_executable(PlanetGenBench
    "bench/BenchReport.cpp"
    "bench/BenchReport.hpp"
    "bench/main.cpp")
target_link_libraries(PlanetGenBench PRIVATE planet_reference planet_gpu bootstrap_vulkan)
set_target_properties(PlanetGenBench PROPERTIES FOLDER "Applications/PlanetGen" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(PlanetGenBench LIBRARY)

# PlanetGenTimelapse: live plate stepping with headless FrameCapture PNG output.
add_executable(PlanetGenTimelapse
    "timelapse/TimelapseAgentTools.cpp"
    "timelapse/TimelapseAgentTools.hpp"
    "timelapse/main.cpp")
target_include_directories(PlanetGenTimelapse PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cooked")
target_link_libraries(PlanetGenTimelapse PRIVATE planet_reference planet_gpu frame_capture agent_tools bootstrap_vulkan)
set_target_properties(PlanetGenTimelapse PROPERTIES FOLDER "Applications/PlanetGen" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(PlanetGenTimelapse LIBRARY)

# PlanetGenLoom: Loom RenderGraph port of PlanetGen with built-in --verify parity check, live interactive renderer, and AgentTools/MCP server.
add_executable(PlanetGenLoom
    "loom/main.cpp"
    "loom/MantlePlumes.cpp"
    "loom/MantlePlumes.hpp"
    "loom/PlanetGraph.cpp"
    "loom/PlanetGraph.hpp"
    "loom/PlanetLiveApp.cpp"
    "loom/PlanetLiveApp.hpp"
    "loom/PlanetLoomTools.cpp"
    "loom/PlanetLoomTools.hpp"
    "${PROJECT_SOURCE_DIR}/tests/integration_tests/fixtures/FrameLimit.cpp"
    "${PROJECT_SOURCE_DIR}/tests/integration_tests/fixtures/FrameLimit.hpp"
    "${PROJECT_SOURCE_DIR}/tests/integration_tests/fixtures/HeadlessBoot.cpp"
    "${PROJECT_SOURCE_DIR}/tests/integration_tests/fixtures/HeadlessBoot.hpp"
    "${PROJECT_SOURCE_DIR}/tests/integration_tests/fixtures/WindowedBoot.cpp"
    "${PROJECT_SOURCE_DIR}/tests/integration_tests/fixtures/WindowedBoot.hpp")
target_include_directories(PlanetGenLoom PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    "${CMAKE_CURRENT_SOURCE_DIR}/loom"
    "${PROJECT_SOURCE_DIR}/tests/integration_tests/fixtures"
    "${PROJECT_SOURCE_DIR}/modules/PlatformSystem/include"
    "${CMAKE_BINARY_DIR}/include")
target_compile_definitions(PlanetGenLoom PRIVATE
    "PLANET_GEN_FONT_DIR=\"${PROJECT_SOURCE_DIR}/assets/fonts\"")
target_link_libraries(PlanetGenLoom PRIVATE
    planet_reference
    planet_gpu
    loom_vulkan
    loom_vulkan_platform
    imgui_module
    platform_system
    agent_tools
    bootstrap_vulkan
    frame_capture
    ${Vulkan_LIBRARY})
add_dependencies(PlanetGenLoom planet_gen_shaders)
set_target_properties(PlanetGenLoom PROPERTIES FOLDER "Applications/PlanetGen" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(PlanetGenLoom LIBRARY)
