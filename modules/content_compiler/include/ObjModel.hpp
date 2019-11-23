#pragma once
#ifndef RESOURCE_CONTEXT_OBJ_MODEL_HPP
#define RESOURCE_CONTEXT_OBJ_MODEL_HPP
#include "utility/tagged_bool.hpp"
#include <cstdint>

struct ObjectModelLoader;

struct PrimitiveGroup
{
    const char* primitiveName;
    uint32_t startIndex{ 0u };
    uint32_t indexCount{ 0u };
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

struct ModelStatistics
{
    uint32_t numFaces{ 0u };
    uint32_t numMaterials{ 0u };
    bool hasNormals;
    bool hasTangents;
};

struct ObjectModelData
{
    uint32_t vertexDataSize{ 0u };
    uint32_t vertexStride{ 0u };
    float* vertexData{ nullptr };
    uint32_t vertexAttributeCount{ 0u };
    VertexMetadataEntry* vertexAttribMetadata{ nullptr };
    uint32_t indexDataSize{ 0u };
    uint32_t* indexData{ nullptr };
    uint32_t numMaterials{ 0u };
    MaterialRange* materialRanges{ nullptr };
    uint32_t numPrimGroups{ 0u };
    PrimitiveGroup* primitiveGroups{ nullptr };
    float minPosition[3]{ 3e38f, 3e38f, 3e38f };
    float maxPosition[3]{-3e38f,-3e38f,-3e38f };
};

using RequiresNormals = tagged_bool<struct RequiresNormalsTag>;
using RequiresTangents = tagged_bool<struct RequiresTangentsTag>;
using OptimizeMesh = tagged_bool<struct OptimizeMeshTag>;

ObjectModelData* LoadModelFromFile(
    const char* model_filename,
    const char* material_directory,
    RequiresNormals requires_normals,
    RequiresTangents requires_tangents,
    OptimizeMesh optimize_mesh);

#endif //!RESOURCE_CONTEXT_OBJ_MODEL_HPP
