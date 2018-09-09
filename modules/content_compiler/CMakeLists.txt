

SET(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "If the supplementary tools for Assimp are built in addition to the library.")
SET(ASSIMP_BUILD_TESTS OFF CACHE BOOL "If the test suite for Assimp is built in addition to the library.")
SET(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "default value of all ASSIMP_BUILD_XXX_IMPORTER value")
SET(ASSIMP_BUILD_OBJ_IMPORTER ON)
SET(ASSIMP_BUILD_GLTF_IMPORTER ON)
ADD_SUBDIRECTORY("../../third_party/assimp" "${CMAKE_CURRENT_BINARY_DIR}/assimp")
SET_TARGET_PROPERTIES(assimp PROPERTIES FOLDER "Assimp")
SET_TARGET_PROPERTIES(zlib PROPERTIES FOLDER "Assimp")
SET_TARGET_PROPERTIES(zlibstatic PROPERTIES FOLDER "Assimp")
SET_TARGET_PROPERTIES(IrrXML PROPERTIES FOLDER "Assimp")
SET_TARGET_PROPERTIES(UpdateAssimpLibsDebugSymbolsAndDLLs PROPERTIES FOLDER "Assimp")

ADD_MODULE(content_compiler
    "include/MeshData.hpp"
    "include/AssimpMeshImporter.hpp"
    "src/AssimpMeshImporter.cpp"
)

TARGET_LINK_LIBRARIES(content_compiler PUBLIC assimp easyloggingpp)
TARGET_INCLUDE_DIRECTORIES(content_compiler PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/include" 
    "../../third_party/assimp/code" 
    "../../third_party/glm"
    "../../third_party/easyloggingpp/src"
    "${Vulkan_INCLUDE_DIR}"
)
