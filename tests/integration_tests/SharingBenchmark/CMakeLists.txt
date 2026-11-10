# Times the raster pass and the compute pass of one 4K target, in exclusive and concurrent sharing mode, on the
# graphics queue. It is a benchmark, not a pass/fail test: it prints a table and one JSON object per case.
lodestone_add_shader_cook(sharing_benchmark_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    MODULES SharingBenchmark.slang)
set_target_properties(sharing_benchmark_shaders PROPERTIES FOLDER "Tests/Integration Tests")

ADD_INTEGRATION_TEST(SharingBenchmark
    "main.cpp"
    "SharingBenchmark.slang")
add_dependencies(SharingBenchmark sharing_benchmark_shaders)
target_compile_definitions(SharingBenchmark PRIVATE
    "SHARING_BENCHMARK_MANIFEST=\"$<TARGET_PROPERTY:sharing_benchmark_shaders,LODESTONE_MANIFEST>\"")
