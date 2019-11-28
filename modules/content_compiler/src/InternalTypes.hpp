#pragma once
#ifndef CONTENT_COMPILER_INTERNAL_TYPES_HPP
#define CONTENT_COMPILER_INTERNAL_TYPES_HPP
#include "MeshData.hpp"
#include <cstdint>
#include <vector>

struct ObjectModelDataImpl
{
    size_t vertexStride{ 0u };
    std::vector<float> vertexData;
    std::vector<VertexMetadataEntry> vertexMetadata;
    std::vector<uint32_t> indices;
    std::vector<MaterialRange> materialRanges;
    std::vector<PrimitiveGroup> primitiveGroups;
    float min[3];
    float max[3];
    explicit operator ObjectModelData() const;
};

struct StoredTextureDataImpl
{
    const char* name{ nullptr };
    size_t nameLen{ 0u };
};

struct LoadedTextureDataImpl
{
    uint32_t vkFormat{ 0u };
    uint32_t width{ 0u };
    uint32_t height{ 0u };
    uint32_t depth{ 0u };
    uint32_t mipLevels{ 0u };
    uint32_t arrayLayers{ 0u };
    uint32_t dataSize;
    // pointer to data usable on the GPU
    void* dataPointer{ nullptr };
};

#endif //!CONTENT_COMPILER_INTERNAL_TYPES_HPP
