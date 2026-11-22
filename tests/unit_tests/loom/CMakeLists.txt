cmake_minimum_required(VERSION 3.10)

set(LOOM_TEST_SOURCES
    test_main.cpp
    test_diagnostics.cpp
    test_builder.cpp
    freeze_test_helpers.hpp
    test_frozen_graph.cpp
    test_freeze_resolve.cpp
    test_freeze_draw.cpp
    test_order.cpp
    test_explain.cpp
    test_resolve.cpp
    test_bind.cpp
    test_size.cpp
    test_layout.cpp
    test_levels.cpp
    test_runtime.cpp
)

add_unit_test_target(loom_tests
    "${LOOM_TEST_SOURCES}"
    "loom")

# The core links no RHI, and its tests run with no GPU
bootstrap_assert_no_rhi(loom_tests)

# The stages and their helpers live in modules/RenderGraph/src. Tests feed them directly.
target_include_directories(loom_tests PRIVATE "${CMAKE_SOURCE_DIR}/modules/RenderGraph/src")

# The Resolve tests read this bundle with no device. Both profiles, so the later Bind tests meet every placement form.
lodestone_add_shader_cook(loom_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv spirv-modern-mutable
    HOST_TYPES "math/ShaderHostTypes.hpp"
    MODULES shaders/LoomResolveTest.slang shaders/LoomResolveOther.slang shaders/LoomBindTest.slang shaders/LoomSizeTest.slang
    shaders/LoomLayoutTest.slang
    # the runtime tests share LoomComputeTest's shaders, so the GPU test and the GPU-free one read one source
    "${CMAKE_SOURCE_DIR}/tests/integration_tests/LoomComputeTest/LoomHashFill.slang"
    "${CMAKE_SOURCE_DIR}/tests/integration_tests/LoomImageTest/LoomImages.slang")
set_target_properties(loom_test_shaders PROPERTIES FOLDER "Tests/Unit Tests")
add_dependencies(loom_tests loom_test_shaders)
target_compile_definitions(loom_tests PRIVATE
    "LOOM_TEST_BUNDLE=\"$<TARGET_PROPERTY:loom_test_shaders,LODESTONE_MANIFEST>\"")
# The generated host header sits beside the manifest
target_include_directories(loom_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cooked")
