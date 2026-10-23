ADD_MODULE(color_math
    "include/ColorSpaces.hpp"
    "include/TransferFunctions.hpp"
    "src/ColorSpaces.cpp"
    "src/TransferFunctions.cpp")

target_include_directories(color_math PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(color_matrix_gen "tools/color_matrix_gen.cpp")
target_link_libraries(color_matrix_gen PRIVATE color_math)
set_target_properties(color_matrix_gen PROPERTIES FOLDER "Modules" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(color_matrix_gen LIBRARY)

# PlatformSystem and foundation do not export their include folders, and the public headers of color_pipe include them
ADD_MODULE(color_pipe
    "include/ColorComposite.hpp"
    "include/ColorPipe.hpp"
    "include/ColorPipeError.hpp"
    "include/DisplayTarget.hpp"
    "src/ColorComposite.cpp"
    "src/ColorPipe.cpp"
    "src/ColorPipeError.cpp"
    "src/FullscreenDrawState.cpp"
    "src/FullscreenDrawState.hpp"
    "src/DisplayTarget.cpp")

target_include_directories(color_pipe PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PlatformSystem/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")
target_link_libraries(color_pipe PUBLIC color_math rhi_resources rhi_system platform_system)

# ddColorGenerated.slang is written at build time. A cook that imports ddColor needs both folders in INCLUDE_DIRS, and a
# dependency on color_pipe_shader_data
set(COLOR_PIPE_SHADER_DIR "${PROJECT_SOURCE_DIR}/assets/shaders/common/Color"
    CACHE INTERNAL "The folder of the ddColor Slang module")
set(COLOR_PIPE_GENERATED_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/shaders/Color"
    CACHE INTERNAL "The folder of ddColorGenerated.slang, which is written at build time")
set(COLOR_PIPE_GENERATED_SLANG "${COLOR_PIPE_GENERATED_SHADER_DIR}/ddColorGenerated.slang")
set(COLOR_PIPE_GENERATED_STAMP "${CMAKE_CURRENT_BINARY_DIR}/color_matrix_gen.stamp")
add_custom_command(
    OUTPUT "${COLOR_PIPE_GENERATED_STAMP}"
    BYPRODUCTS "${COLOR_PIPE_GENERATED_SLANG}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${COLOR_PIPE_GENERATED_SHADER_DIR}"
    COMMAND color_matrix_gen "${COLOR_PIPE_GENERATED_SLANG}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${COLOR_PIPE_GENERATED_STAMP}"
    DEPENDS color_matrix_gen
    COMMENT "Generating ddColorGenerated.slang"
    VERBATIM)
add_custom_target(color_pipe_shader_data DEPENDS "${COLOR_PIPE_GENERATED_STAMP}")
set_target_properties(color_pipe_shader_data PROPERTIES FOLDER "Modules")

set(COLOR_PIPE_SHADER_MODULE "${COLOR_PIPE_SHADER_DIR}/ddColor.slang"
    CACHE INTERNAL "The ddColor Slang module, for consumers to cook into their bundles")

set(COLOR_PIPE_COMPOSITE_SHADER_DIR "${PROJECT_SOURCE_DIR}/assets/shaders/shading/ColorPipe"
    CACHE INTERNAL "The folder of the ColorComposite Slang module")
set(COLOR_PIPE_COMPOSITE_SHADER_MODULE "${COLOR_PIPE_COMPOSITE_SHADER_DIR}/ColorComposite.slang"
    CACHE INTERNAL "The ColorComposite Slang module, for consumers to cook into their bundles")
set(COLOR_PIPE_COMPOSITE_POLICY_FILE "${COLOR_PIPE_COMPOSITE_SHADER_DIR}/ColorCompositePolicy.toml"
    CACHE INTERNAL "The cook policy of ColorComposite")
set(COLOR_PIPE_COMPOSITE_INCLUDE_DIRS "${COLOR_PIPE_SHADER_DIR};${COLOR_PIPE_GENERATED_SHADER_DIR}"
    CACHE INTERNAL "The INCLUDE_DIRS that a cook of ColorComposite needs")
