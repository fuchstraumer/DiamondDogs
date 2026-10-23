lodestone_add_shader_cook(color_pipe_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    POLICY_FILE "${COLOR_PIPE_COMPOSITE_POLICY_FILE}"
    INCLUDE_DIRS ${COLOR_PIPE_COMPOSITE_INCLUDE_DIRS}
    MODULES "${COLOR_PIPE_COMPOSITE_SHADER_MODULE}")
# the cook reads ddColorGenerated.slang, so the generator runs first
add_dependencies(color_pipe_test_shaders color_pipe_shader_data)
set_target_properties(color_pipe_test_shaders PROPERTIES FOLDER "Tests/Integration Tests")

ADD_INTEGRATION_TEST(ColorPipeTest "main.cpp")
add_dependencies(ColorPipeTest color_pipe_test_shaders)
target_link_libraries(ColorPipeTest PRIVATE color_pipe rhi_resources)
target_compile_definitions(ColorPipeTest PRIVATE
    "COLOR_PIPE_TEST_MANIFEST=\"$<TARGET_PROPERTY:color_pipe_test_shaders,LODESTONE_MANIFEST>\"")

add_test(NAME ColorPipeTest COMMAND ColorPipeTest)
