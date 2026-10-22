# Cooks NvproPyramid on its own, runs pyramid::MipGenerator headless, and checks every level of every layer against a
# CPU reference: power-of-two, odd and non-square sizes, the average, min and max reductions, and the fp16 cache.
lodestone_add_shader_cook(gpu_pyramid_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    MODULES "${GPU_PYRAMID_SHADER_MODULE}")
set_target_properties(gpu_pyramid_test_shaders PROPERTIES FOLDER "Tests/Integration Tests")

ADD_INTEGRATION_TEST(GpuPyramidTest "main.cpp")
add_dependencies(GpuPyramidTest gpu_pyramid_test_shaders)
target_link_libraries(GpuPyramidTest PRIVATE gpu_pyramid rhi_resources)
target_compile_definitions(GpuPyramidTest PRIVATE
    "GPU_PYRAMID_TEST_MANIFEST=\"$<TARGET_PROPERTY:gpu_pyramid_test_shaders,LODESTONE_MANIFEST>\"")

add_test(NAME GpuPyramidTest COMMAND GpuPyramidTest)
