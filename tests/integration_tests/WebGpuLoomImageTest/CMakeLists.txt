# LoomImageTest on WebGPU: the same seven passes (a storage image then sampled, an imported image that sizes a
# dispatch, history rings on a buffer and an image with OnNoHistory::Clear, WhenInputsChange and OnTrigger), cooked to
# WGSL, against the same CPU values. It runs in a browser.
lodestone_add_shader_cook(webgpu_loom_image_test_shaders
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/cooked"
    TARGETS wgsl
    HOST_TYPES "math/ShaderHostTypes.hpp"
    MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../LoomImageTest/LoomImages.slang")
set_target_properties(webgpu_loom_image_test_shaders PROPERTIES FOLDER "Tests/Integration Tests")

add_executable(WebGpuLoomImageTest
    "main.cpp"
)

set_target_properties(WebGpuLoomImageTest PROPERTIES
    SUFFIX ".html"
    FOLDER "Tests/Integration Tests"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
)

add_dependencies(WebGpuLoomImageTest webgpu_loom_image_test_shaders)
target_include_directories(WebGpuLoomImageTest PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cooked")

target_link_libraries(WebGpuLoomImageTest PRIVATE
    bootstrap_webgpu
    loom_webgpu
    rhi_resources
    rhi_context
    shader_bundle
    emdawnwebgpu_cpp
    foundation
)

target_link_options(WebGpuLoomImageTest PRIVATE
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
    "--embed-file" "${CMAKE_CURRENT_BINARY_DIR}/cooked/ShaderLibrary.ldmanifest@/ShaderLibrary.ldmanifest"
    "-sEXIT_RUNTIME=0"
    "-sDYNAMIC_EXECUTION=0"
    "-sINITIAL_MEMORY=32MB"
    "-sALLOW_MEMORY_GROWTH=1"
    "$<IF:$<BOOL:${DIAMOND_DOGS_WEB_THREADS}>,-sENVIRONMENT=web$<COMMA>worker,-sENVIRONMENT=web>"
)

set_property(TARGET WebGpuLoomImageTest APPEND PROPERTY
    LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
        "${CMAKE_CURRENT_BINARY_DIR}/cooked/ShaderLibrary.ldmanifest"
)
