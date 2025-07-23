#pragma once
#ifndef ASSET_PIPELINE_MESH_DATA_HPP
#define ASSET_PIPELINE_MESH_DATA_HPP
#include <cstdint>
#include <string>

struct ccDataHandle
{
    uint32_t typeHash;
    uint32_t containerIdx;
    uint64_t dataHash;
};

using ccDataHandle = uint64_t;

namespace detail
{
    // FNV-1a 32bit hashing algorithm.
    constexpr std::uint32_t fnv1a_32(char const* s, std::size_t count)
    {
        return ((count ? fnv1a_32(s, count - 1) : 2166136261u) ^ s[count]) * 16777619u;
    }
}

constexpr std::uint32_t operator"" _ccHashName(char const* s, std::size_t count)
{
    return detail::fnv1a_32(s, count);
}

struct PrimitiveGroup
{
    std::string primitiveName;
    uint32_t startMaterial{ 0u };
    uint32_t materialCount{ 0u };
    float min[3]{ 3e38f, 3e38f, 3e38f };
    float max[3]{-3e38f,-3e38f,-3e38f };
};

struct MaterialRange
{
    std::string MaterialName;
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
    ccDataHandle materialFile{ std::numeric_limits<ccDataHandle>::max() };
};

struct cStringArray
{
    cStringArray() = default;
    cStringArray(const size_t num_strings);
    ~cStringArray();
    cStringArray(const cStringArray& other) noexcept;
    cStringArray& operator=(const cStringArray& other) noexcept;
    cStringArray(cStringArray&& other) noexcept;
    cStringArray& operator=(cStringArray&& other) noexcept;

    const char* operator[](const size_t idx) const;
    void set_string(const size_t idx, const char* str);
    size_t num_strings() const noexcept;

private:
    void destroy();
    void clone(cStringArray& other);
    char** strings{ nullptr };
    size_t numStrings{ 0u };
};

#endif //!ASSET_PIPELINE_MESH_DATA_HPP
