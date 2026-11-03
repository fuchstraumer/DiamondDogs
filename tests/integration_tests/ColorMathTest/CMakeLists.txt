# Cooks ColorMathKernels.slang with ddColor (assets/shaders/common/Color), runs every ddColor function on the GPU
# headless, and compares each output with the double precision reference in modules/ColorPipe. This also proves the
# matrix convention: the Slang constants are the transpose of the C++ matrices, and a wrong transpose fails here.

lodestone_add_shader_cook(color_math_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    INCLUDE_DIRS "${COLOR_PIPE_SHADER_DIR}" "${COLOR_PIPE_GENERATED_SHADER_DIR}"
    MODULES "${CMAKE_CURRENT_SOURCE_DIR}/ColorMathKernels.slang")
# the cook reads ddColorGenerated.slang, so the generator runs first
add_dependencies(color_math_test_shaders color_pipe_shader_data)
set_target_properties(color_math_test_shaders PROPERTIES FOLDER "Tests/Integration Tests")

ADD_INTEGRATION_TEST(ColorMathTest
    "main.cpp"
    "ColorMathKernels.slang")
add_dependencies(ColorMathTest color_math_test_shaders)
target_link_libraries(ColorMathTest PRIVATE color_math rhi_resources)
target_compile_definitions(ColorMathTest PRIVATE
    "COLOR_MATH_TEST_MANIFEST=\"$<TARGET_PROPERTY:color_math_test_shaders,LODESTONE_MANIFEST>\"")

add_test(NAME ColorMathTest COMMAND ColorMathTest)
