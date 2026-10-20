# Cooks HashFill.slang with Lodestone as a build step, then runs it headless through rhi::ShaderLibrary and
# compares the output with a CPU reference. No window and no swapchain: this is the compute path that a
# noise graph host uses.
lodestone_add_shader_cook(compute_manifest_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    MODULES HashFill.slang)
set_target_properties(compute_manifest_test_shaders PROPERTIES FOLDER "Tests/Integration Tests")

ADD_INTEGRATION_TEST(ComputeManifestTest
    "main.cpp"
    "HashFill.slang")
add_dependencies(ComputeManifestTest compute_manifest_test_shaders)
target_compile_definitions(ComputeManifestTest PRIVATE
    "COMPUTE_MANIFEST_TEST_MANIFEST=\"$<TARGET_PROPERTY:compute_manifest_test_shaders,LODESTONE_MANIFEST>\"")
copy_slang_dlls_to_target(ComputeManifestTest)

add_test(NAME ComputeManifestTest COMMAND ComputeManifestTest)
