# Color math for the HDR pipe: gamut matrices derived from chromaticities, luminance, transfer functions and Kelvin, in
# double precision. This is the CPU reference. The Slang module ddColor (assets/shaders/common/Color) has the same
# functions in float, and its matrices are written by color_matrix_gen from this library.
ADD_MODULE(color_pipe
    "include/ColorSpaces.hpp"
    "include/TransferFunctions.hpp"
    "src/ColorSpaces.cpp"
    "src/TransferFunctions.cpp")

target_include_directories(color_pipe PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(color_matrix_gen "tools/color_matrix_gen.cpp")
target_link_libraries(color_matrix_gen PRIVATE color_pipe)
set_target_properties(color_matrix_gen PROPERTIES FOLDER "Modules" CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
diamond_dogs_add_pch(color_matrix_gen LIBRARY)

# ddColorGenerated.slang has to sit next to ddColor.slang, because the cooker only searches a module's own folder, so
# it is written into the source tree (only when its contents change). A cook of ddColor needs the output of this
# command: add a dependency on color_pipe_shader_data to the cook target.
set(COLOR_PIPE_SHADER_DIR "${PROJECT_SOURCE_DIR}/assets/shaders/common/Color"
    CACHE INTERNAL "The folder of the ddColor Slang module and its generated matrices")
set(COLOR_PIPE_GENERATED_SLANG "${COLOR_PIPE_SHADER_DIR}/ddColorGenerated.slang")
set(COLOR_PIPE_GENERATED_STAMP "${CMAKE_CURRENT_BINARY_DIR}/color_matrix_gen.stamp")
add_custom_command(
    OUTPUT "${COLOR_PIPE_GENERATED_STAMP}"
    BYPRODUCTS "${COLOR_PIPE_GENERATED_SLANG}"
    COMMAND color_matrix_gen "${COLOR_PIPE_GENERATED_SLANG}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${COLOR_PIPE_GENERATED_STAMP}"
    DEPENDS color_matrix_gen
    COMMENT "Generating ddColorGenerated.slang"
    VERBATIM)
add_custom_target(color_pipe_shader_data DEPENDS "${COLOR_PIPE_GENERATED_STAMP}")
set_target_properties(color_pipe_shader_data PROPERTIES FOLDER "Modules")

# The module has no bundle of its own. A consumer makes its cook target depend on color_pipe_shader_data. The cooker
# cannot import a module that sits outside the importer's folder and that folder's parent (see ColorMathTest), so a
# consumer has to stage a copy of this file and of ddColorGenerated.slang next to its shader until the cooker has
# include search paths.
set(COLOR_PIPE_SHADER_MODULE "${COLOR_PIPE_SHADER_DIR}/ddColor.slang"
    CACHE INTERNAL "The ddColor Slang module, for consumers to cook into their bundles")
