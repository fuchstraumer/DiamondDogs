# Loom step 6 on the GPU: images and samplers (layout changes, an imported image whose extent sizes a dispatch), history
# rings (a buffer and an image, OnNoHistory::Clear), and the WhenInputsChange and OnTrigger run policies, headless.
lodestone_add_shader_cook(loom_image_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS spirv
    HOST_TYPES "math/ShaderHostTypes.hpp"
    MODULES LoomImages.slang)
set_target_properties(loom_image_test_shaders PROPERTIES FOLDER "Tests/Integration Tests")

ADD_INTEGRATION_TEST(LoomImageTest
    "main.cpp"
    "LoomImages.slang"
    "../fixtures/HeadlessBoot.hpp"
    "../fixtures/HeadlessBoot.cpp")
add_dependencies(LoomImageTest loom_image_test_shaders)
target_link_libraries(LoomImageTest PRIVATE bootstrap_vulkan loom_vulkan)
target_compile_definitions(LoomImageTest PRIVATE
    "LOOM_IMAGE_TEST_MANIFEST=\"$<TARGET_PROPERTY:loom_image_test_shaders,LODESTONE_MANIFEST>\"")
# the generated host header, with the Params type of each entry point, sits beside the manifest
target_include_directories(LoomImageTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cooked")

add_test(NAME LoomImageTest COMMAND LoomImageTest)
