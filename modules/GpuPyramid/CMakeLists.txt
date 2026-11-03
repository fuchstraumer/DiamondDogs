# Fills mip chains on the GPU with NvproPyramid (assets/shaders/compute/Pyramid), a Slang transcription of NVIDIA's
# nvpro_pyramid from the vk_compute_mipmaps sample (Apache 2.0, LICENSE.nvpro_pyramid next to the shaders).
ADD_MODULE(gpu_pyramid
    "include/GpuPyramid.hpp"
    "src/GpuPyramid.cpp")

target_include_directories(gpu_pyramid PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../foundation/include")
target_link_libraries(gpu_pyramid PUBLIC rhi_resources rhi_system)

# The module has no bundle of its own: a consumer adds this file to the MODULES of its lodestone_add_shader_cook, so
# one ShaderLibrary serves the consumer's shaders and the pyramid
set(GPU_PYRAMID_SHADER_MODULE "${PROJECT_SOURCE_DIR}/assets/shaders/compute/Pyramid/NvproPyramid.slang"
    CACHE INTERNAL "The NvproPyramid Slang module, for consumers to cook into their bundles")
