# Cooks ColorMathKernels.slang with ddColor (assets/shaders/common/Color), runs every ddColor function on the GPU
# headless, and compares each output with the double precision reference in modules/ColorPipe. This also proves the
# matrix convention: the Slang constants are the transpose of the C++ matrices, and a wrong transpose fails here.
#
# Lodestone limit: a module that imports another module fails in worker setup (ModuleLoadFailed) when the imported
# module is also in the cook's module list, and the cooker finds an imported module only in the importer's folder or
# in that folder's parent. ddColor is in neither, so the cook runs on copies of both in the build directory, and only
# the kernel module is in the list. Remove the staging when the cooker has include search paths.
set(COLOR_MATH_STAGED_DIR "${CMAKE_CURRENT_BINARY_DIR}/staged")
set(COLOR_MATH_STAGED_KERNEL "${COLOR_MATH_STAGED_DIR}/ColorMathKernels.slang")
set(COLOR_MATH_STAGED_FILES
    "${COLOR_MATH_STAGED_KERNEL}"
    "${COLOR_MATH_STAGED_DIR}/ddColor.slang"
    "${COLOR_MATH_STAGED_DIR}/ddColorGenerated.slang")
add_custom_command(
    OUTPUT ${COLOR_MATH_STAGED_FILES}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${COLOR_MATH_STAGED_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/ColorMathKernels.slang" "${COLOR_MATH_STAGED_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${COLOR_PIPE_SHADER_MODULE}" "${COLOR_PIPE_SHADER_DIR}/ddColorGenerated.slang" "${COLOR_MATH_STAGED_DIR}"
    DEPENDS color_pipe_shader_data "${CMAKE_CURRENT_SOURCE_DIR}/ColorMathKernels.slang" "${COLOR_PIPE_SHADER_MODULE}"
        "${COLOR_PIPE_SHADER_DIR}/ddColorGenerated.slang"
    COMMENT "Staging ddColor next to ColorMathKernels"
    VERBATIM)

lodestone_add_shader_cook(color_math_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    MODULES "${COLOR_MATH_STAGED_KERNEL}")
set_target_properties(color_math_test_shaders PROPERTIES FOLDER "Tests/Integration Tests")

ADD_INTEGRATION_TEST(ColorMathTest
    "main.cpp"
    "ColorMathKernels.slang")
add_dependencies(ColorMathTest color_math_test_shaders)
target_link_libraries(ColorMathTest PRIVATE color_pipe rhi_resources)
target_compile_definitions(ColorMathTest PRIVATE
    "COLOR_MATH_TEST_MANIFEST=\"$<TARGET_PROPERTY:color_math_test_shaders,LODESTONE_MANIFEST>\"")

add_test(NAME ColorMathTest COMMAND ColorMathTest)
