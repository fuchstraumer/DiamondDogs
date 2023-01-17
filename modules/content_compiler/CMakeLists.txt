
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
)
