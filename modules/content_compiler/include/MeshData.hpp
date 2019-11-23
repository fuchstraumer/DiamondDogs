#pragma once
#ifndef ASSET_PIPELINE_MESH_DATA_HPP
#define ASSET_PIPELINE_MESH_DATA_HPP
#include <cstdint>

struct PrimitiveGroup
{
    const char* primitiveName;
    uint32_t startMaterial{ 0u };
    uint32_t materialCount{ 0u };
    float min[3]{ 3e38f, 3e38f, 3e38f };
    float max[3]{-3e38f,-3e38f,-3e38f };
};

struct MaterialRange
{
    const char* MaterialName;
    uint32_t startIndex{ 0u };
    uint32_t indexCount{ 0u };
};

struct VertexMetadataEntry
{
    // Where it will be read in the shader. Normally works with what vertex binding this is in, but we don't make that restriction
    // This really just indicates where this vertex falls in the structure describing a whole vertex
    uint32_t location{ 0u };
    // Not using the format flags here just so we don't force that exposed thru the API, but this is converted directly from the VkFormat
    // value used for a vertex when parsing
    uint32_t vkFormat{ 0u };
    // Just regular ol' offset to this data in the vertex as a whole
    uint32_t offset{ 0u };
};

struct ObjectModelData
{
    uint32_t vertexDataSize{ 0u };
    uint32_t vertexStride{ 0u };
    const float* vertexData{ nullptr };
    uint32_t vertexAttributeCount{ 0u };
    const VertexMetadataEntry* vertexAttribMetadata{ nullptr };
    uint32_t indexDataSize{ 0u };
    const uint32_t* indexData{ nullptr };
    uint32_t numMaterials{ 0u };
    const MaterialRange* materialRanges{ nullptr };
    uint32_t numPrimGroups{ 0u };
    const PrimitiveGroup* primitiveGroups{ nullptr };
    float minPosition[3]{ 3e38f, 3e38f, 3e38f };
    float maxPosition[3]{-3e38f,-3e38f,-3e38f };
};

// Upper half of file content hash
struct ccDataHandle
{
    uint64_t low;
    uint64_t high;
};

#endif //!ASSET_PIPELINE_MESH_DATA_HPP
