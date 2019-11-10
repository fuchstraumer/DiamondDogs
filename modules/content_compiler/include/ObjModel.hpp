#pragma once
#ifndef RESOURCE_CONTEXT_OBJ_MODEL_HPP
#define RESOURCE_CONTEXT_OBJ_MODEL_HPP
#include "ResourceTypes.hpp"
#include "MeshData.hpp"
#include <vector>
#include <set>
#include <map>
#include "MaterialCache.hpp"

class DescriptorBinder;

class ObjectModel
{
public:

    ObjectModel();
    void LoadModelFromFile(const char* model_filename, const char* material_directory);

    struct vtxPosition
    {
        float x;
        float y;
        float z;
    };

    struct AABB
    {
        vtxPosition Min;
        vtxPosition Max;
    };

    struct vtxAttrib
    {
        vtxPosition normal;
        vtxPosition tangent;
        float uv[2];
    };

    struct Part
    {
        uint32_t startIdx;
        uint32_t idxCount;
        int32_t vertexOffset;
        MaterialInstance material;
        AABB aabb;
        constexpr bool operator==(const Part& other) const noexcept;
        constexpr bool operator<(const Part& other) const noexcept;
    };

private:

    std::vector<vtxPosition> positions;
    std::vector<vtxAttrib> attributes;
    // we first buffer everything in here, then later
    // we copy into indicesUi16 and clear this if we can
    std::vector<uint32_t> indicesUi32;
    std::vector<uint16_t> indicesUi16;
    bool indicesAreUi16;
    uint32_t vtxCount{ 0u };
    uint32_t idxCount{ 0u };
    AABB modelAABB;

};

#endif //!RESOURCE_CONTEXT_OBJ_MODEL_HPP
