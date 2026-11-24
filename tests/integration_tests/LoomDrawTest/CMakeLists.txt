# Loom step 7 on the GPU, headless: a compute cull, a depth prepass through DrawIndirectCount, a two-target GBuffer
# with depth Equal, and a fullscreen light pass that reads LoomFrame and writes an offscreen frame target.
lodestone_add_shader_cook(loom_draw_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    HOST_TYPES "math/ShaderHostTypes.hpp"
    # LoomDraw imports LoomFrame
    INCLUDE_DIRS "${LOOM_SHADER_DIR}"
    MODULES LoomDraw.slang)
set_target_properties(loom_draw_test_shaders PROPERTIES FOLDER "Tests/Integration Tests")

ADD_INTEGRATION_TEST(LoomDrawTest
    "main.cpp"
    "LoomDrawGraph.hpp"
    "LoomDrawGraph.cpp"
    "LoomDraw.slang"
    "../fixtures/HeadlessBoot.hpp"
    "../fixtures/HeadlessBoot.cpp")
add_dependencies(LoomDrawTest loom_draw_test_shaders)
target_link_libraries(LoomDrawTest PRIVATE bootstrap_vulkan loom_vulkan)
target_compile_definitions(LoomDrawTest PRIVATE
    "LOOM_DRAW_TEST_MANIFEST=\"$<TARGET_PROPERTY:loom_draw_test_shaders,LODESTONE_MANIFEST>\"")
target_include_directories(LoomDrawTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cooked")

add_test(NAME LoomDrawTest COMMAND LoomDrawTest)
