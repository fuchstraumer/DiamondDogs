# Loom's first GPU proof (spec step 5): a two-pass compute graph through Compile, Realize and Tick, headless. Every
# output word is compared with the CPU, and synchronization validation checks the barriers that Loom places.
lodestone_add_shader_cook(loom_compute_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    HOST_TYPES "math/ShaderHostTypes.hpp"
    MODULES LoomHashFill.slang)
set_target_properties(loom_compute_test_shaders PROPERTIES FOLDER "Tests/Integration Tests")

ADD_INTEGRATION_TEST(LoomComputeTest
    "main.cpp"
    "LoomHashFill.slang"
    "../fixtures/HeadlessBoot.hpp"
    "../fixtures/HeadlessBoot.cpp")
add_dependencies(LoomComputeTest loom_compute_test_shaders)
target_link_libraries(LoomComputeTest PRIVATE bootstrap_vulkan loom_vulkan)
target_compile_definitions(LoomComputeTest PRIVATE
    "LOOM_COMPUTE_TEST_MANIFEST=\"$<TARGET_PROPERTY:loom_compute_test_shaders,LODESTONE_MANIFEST>\"")
# the generated host header, with the Params type of each entry point, sits beside the manifest
target_include_directories(LoomComputeTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cooked")

add_test(NAME LoomComputeTest COMMAND LoomComputeTest)
