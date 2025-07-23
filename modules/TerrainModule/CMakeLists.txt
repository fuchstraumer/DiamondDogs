
ADD_MODULE(terrain_module
    "include/AABB.hpp"
    "include/HeightNode.hpp"
    "include/HeightSample.hpp"
    "include/Noise.hpp"
    "include/NoiseGen.hpp"
    "include/PlanarMesh.hpp"
    "include/TerrainNode.hpp"
    "include/TerrainQuadtree.hpp"
    "src/HeightNode.cpp"
    "src/HeightSample.cpp"
    "src/Noise.cpp"
    "src/NoiseGen.cpp"
    "src/PlanarMesh.cpp"
    "src/TerrainNode.cpp"
    "src/TerrainQuadtree.cpp"
)

TARGET_INCLUDE_DIRECTORIES(terrain_module PRIVATE
    ${VPR_INCLUDE_DIRS_MODULE_RELATIVE}
    ${Vulkan_INCLUDE_DIR}
    "../../third_party/glm"
    "../ecs_module/include/"
)

TARGET_LINK_LIBRARIES(terrain_module PUBLIC vpr_core ${Vulkan_LIBRARY} easyloggingpp)
