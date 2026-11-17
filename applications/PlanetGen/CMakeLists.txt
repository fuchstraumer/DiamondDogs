# PlanetGen: the C++ and Lodestone port of the PlanetLab plate model. The Python prototype and its reference files
# live in applications/PlanetLab, which git ignores. README.md explains the parts.
set(PLANET_GEN_SHADER_DIR "${PROJECT_SOURCE_DIR}/assets/shaders/compute/Planet")

lodestone_add_shader_cook(planet_gen_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    OPTIONS --O3 --g3
    MODULES "${PLANET_GEN_SHADER_DIR}/PlanetLab.slang")
set_target_properties(planet_gen_shaders PROPERTIES FOLDER "Applications/PlanetGen")

# planet_gpu: the programs of the PlanetLab module with their reflected bindings, and named buffers
add_library(planet_gpu STATIC
    "gpu/GpuBuffers.cpp"
    "gpu/GpuBuffers.hpp"
    "gpu/PlanetGpuErrors.cpp"
    "gpu/PlanetGpuErrors.hpp"
    "gpu/PlanetShaders.cpp"
    "gpu/PlanetShaders.hpp")
target_include_directories(planet_gpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/gpu")
target_link_libraries(planet_gpu PUBLIC bootstrap_vulkan rhi_resources rhi_system foundation lodestone::client_library)
target_compile_definitions(planet_gpu PUBLIC
    "NOMINMAX"
    "PLANET_GEN_MANIFEST=\"$<TARGET_PROPERTY:planet_gen_shaders,LODESTONE_MANIFEST>\"")
set_target_properties(planet_gpu PROPERTIES FOLDER "Applications/PlanetGen" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
add_dependencies(planet_gpu planet_gen_shaders)
diamond_dogs_add_pch(planet_gpu LIBRARY)

# PlanetGenCheck: each pass against the reference files of PlanetLab. Headless, with Full validation
add_executable(PlanetGenCheck
    "check/main.cpp"
    "check/ReferenceSet.cpp"
    "check/ReferenceSet.hpp")
target_include_directories(PlanetGenCheck PRIVATE "${PROJECT_SOURCE_DIR}/third_party/json/include")
target_link_libraries(PlanetGenCheck PRIVATE planet_gpu bootstrap_vulkan)
set_target_properties(PlanetGenCheck PROPERTIES FOLDER "Applications/PlanetGen" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(PlanetGenCheck LIBRARY)
