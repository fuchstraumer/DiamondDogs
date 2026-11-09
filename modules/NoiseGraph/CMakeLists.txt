# The noise graph. Today this folder has only the shader side: the Slang field library in assets/shaders/compute/Noise
# (hash, bases, fractal, octave rules, nodes) and its cook. The C++ side comes with the content compiler kinds that run
# the noise nodes. The README in the shader folder explains the library and credits its sources.
set(NOISE_SHADER_DIR "${PROJECT_SOURCE_DIR}/assets/shaders/compute/Noise"
    CACHE INTERNAL "The folder of the noise field library")

# The modules with entry points. The library modules (NoiseHash, NoiseLattice, NoiseField, NoiseBases, NoiseFractal,
# NoiseRules, NoiseNodes, NoiseGrids) have none: these two import them, so the depfile re-cooks on any change
set(NOISE_SHADER_MODULES
    "${NOISE_SHADER_DIR}/NoiseKernels.slang"
    "${NOISE_SHADER_DIR}/NoiseSampleGraph.slang"
    CACHE INTERNAL "The noise Slang modules that declare entry points, for consumers to cook into their bundles")

# SPIR-V only: DiamondDogs builds Lodestone without Tint, so a wgsl cook here has no validator.
# assets/scripts/cook_noise_wgsl.ps1 cooks wgsl with Lodestone's own build, which has Tint
lodestone_add_shader_cook(noise_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    OPTIONS --quiet
    MODULES ${NOISE_SHADER_MODULES})
set_target_properties(noise_shaders PROPERTIES FOLDER "Modules")

# A headless compute app: renders sample heightmaps of the fractal rules to PNG, and compares the autodiff gradient
# with central differences. Run it with an output folder: NoiseHeightmaps <folder>
add_executable(NoiseHeightmaps "NoiseHeightmaps/main.cpp")
add_dependencies(NoiseHeightmaps noise_shaders)
target_include_directories(NoiseHeightmaps PRIVATE
    "${PROJECT_SOURCE_DIR}/foundation/include"
    "${PROJECT_SOURCE_DIR}/modules/RhiSystem/include"
    "${PROJECT_SOURCE_DIR}/third_party/stb")
target_link_libraries(NoiseHeightmaps PRIVATE rhi_resources rhi_system foundation)
target_compile_definitions(NoiseHeightmaps PRIVATE
    "NOMINMAX"
    "NOISE_HEIGHTMAPS_MANIFEST=\"$<TARGET_PROPERTY:noise_shaders,LODESTONE_MANIFEST>\"")
set_target_properties(NoiseHeightmaps PROPERTIES FOLDER "Applications" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(NoiseHeightmaps LIBRARY)
# cmake -E copy fails on an empty list, so the copy runs only when the app links a shared library
add_custom_command(TARGET NoiseHeightmaps POST_BUILD
    COMMAND "$<$<BOOL:$<TARGET_RUNTIME_DLLS:NoiseHeightmaps>>:${CMAKE_COMMAND};-E;copy;$<TARGET_RUNTIME_DLLS:NoiseHeightmaps>;$<TARGET_FILE_DIR:NoiseHeightmaps>>"
    COMMAND_EXPAND_LISTS)
