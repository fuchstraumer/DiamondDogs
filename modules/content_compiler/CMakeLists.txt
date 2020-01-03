

set(basisu_encoder_sources
    "../../third_party/basis_universal/basisu_backend.cpp"
    "../../third_party/basis_universal/basisu_basis_file.cpp"
    "../../third_party/basis_universal/basisu_comp.cpp"
    "../../third_party/basis_universal/basisu_enc.cpp"
    "../../third_party/basis_universal/basisu_etc.cpp"
    "../../third_party/basis_universal/basisu_frontend.cpp"
    "../../third_party/basis_universal/basisu_global_selector_palette_helpers.cpp"
    "../../third_party/basis_universal/basisu_gpu_texture.cpp"
    "../../third_party/basis_universal/basisu_pvrtc1_4.cpp"
    "../../third_party/basis_universal/basisu_resampler.cpp"
    "../../third_party/basis_universal/basisu_resample_filters.cpp"
    "../../third_party/basis_universal/basisu_ssim.cpp"
    "../../third_party/basis_universal/basisu_tool.cpp"
    "../../third_party/basis_universal/basisu_astc_decomp.cpp"
    "../../third_party/basis_universal/lodepng.cpp"
    "../../third_party/basis_universal/transcoder/basisu_transcoder.cpp"
)

source_group("basis_universal" FILES ${basisu_encoder_sources})

ADD_MODULE(content_compiler
    "include/ContentCompilerAPI.hpp"
    "include/ContentCompilerTypes.hpp"
    "include/MeshData.hpp"
    "include/MeshPrimitives.hpp"
    "include/MeshProcessing.hpp"
    "include/ObjMaterial.hpp"
    "include/ObjModel.hpp"
    "src/ContentCompilerAPI.cpp"
    "src/ContentCompilerImpl.hpp"
    "src/ContentCompilerImpl.cpp"
    "src/InternalTypes.hpp"
    "src/LoadedDataCache.hpp"
    "src/LoadedDataCache.cpp"
    "src/MeshData.cpp"
    "src/MeshPrimitives.cpp"
    "src/MeshProcessing.cpp"
    "src/ObjMaterial.cpp"
    "src/ObjModel.cpp"
    "src/svUtil.hpp"
    "src/svUtil.cpp"
    ${basisu_encoder_sources}
)

TARGET_LINK_LIBRARIES(content_compiler PUBLIC easyloggingpp)
TARGET_INCLUDE_DIRECTORIES(content_compiler PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "../../third_party/glm"
    "../../third_party/easyloggingpp/src"
    "${Vulkan_INCLUDE_DIR}"
    "../../third_party/tinyobjloader/experimental"
    "../material_module/include"
    "../resource_context/include"
    "../rendering_context/include"
    "../../third_party/mango/include"
    "../../third_party/basis_universal"
    "../../third_party/basis_universal/transcoder"
)
