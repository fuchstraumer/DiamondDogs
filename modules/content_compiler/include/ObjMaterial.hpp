#pragma once
#ifndef CONTENT_COMPILER_OBJ_MATERIAL_HPP
#define CONTENT_COMPILER_OBJ_MATERIAL_HPP
#include "MeshData.hpp"

constexpr static uint32_t ObjMaterialDataHashCode = "ObjMaterialData"_ccHashName;
constexpr static uint32_t ObjMaterialTexturesHashCode = "ObjMaterialTextures"_ccHashName;
constexpr static uint32_t ObjMaterialHashCode = "ObjMaterial"_ccHashName;
constexpr static uint32_t ObjMaterialFileHashCode = "ObjMaterialFile"_ccHashName;

struct ObjMaterialData
{
    float ambient[3]{ 3e38f, 3e38f, 3e38f };
    float diffuse[3]{ 3e38f, 3e38f, 3e38f };
    float specular[3]{ 3e38f, 3e38f, 3e38f };
    float transmittance[3]{ 3e38f, 3e38f, 3e38f };
    float emission[3]{ 3e38f, 3e38f, 3e38f };
    float shininess{ 3e38f };
    float ior{ 3e38f };
    float dissolve{ 3e38f };
    int illuminationModel{ -1 };
    // PBR extensions
    float roughness;
    float metallic;
    float sheen;
    float clearcoatThickness;
    float clearcoatRoughness;
    float anisotropy;
    float anisotropyRotation;
};

// Stores indices (32 bits) instead of handles (128 bits)
// to what textures this material uses, as stored in the parent material
// file
struct ObjMaterialTextures
{
    // used so that we know to set the actual index to our safe null descriptors, where those may be 
    // at the time we set these up in rendering code
    constexpr static uint32_t INVALID_IDX = std::numeric_limits<uint32_t>::max();
    uint32_t ambient{ INVALID_IDX };
    uint32_t diffuse{ INVALID_IDX };
    uint32_t specular{ INVALID_IDX };
    uint32_t specularHighlight{ INVALID_IDX };
    uint32_t bump{ INVALID_IDX };
    uint32_t displacement{ INVALID_IDX };
    uint32_t alpha{ INVALID_IDX };
    uint32_t reflection{ INVALID_IDX };
    // PBR extensions
    uint32_t roughness{ INVALID_IDX };
    uint32_t metallic{ INVALID_IDX };
    uint32_t sheen{ INVALID_IDX };
    uint32_t emissive{ INVALID_IDX };
    uint32_t normal{ INVALID_IDX };
};

struct ObjMaterial
{
    ObjMaterialData data;
    ObjMaterialTextures textures;
};

struct ParsedMtlFileData
{
    size_t numMaterials{ 0u };
    ObjMaterial* materials{ nullptr };
    cStringArray materialNames;
    cStringArray uniqueTexturePaths;
};

struct LoadedMaterialData
{
    size_t numMaterials{ 0u };
    ObjMaterial* materials{ nullptr };
    cStringArray materialNames;
    size_t numTextures{ 0u };
    ccDataHandle* textures{ nullptr };
};

// Expects directory to search to be in userData. Parses .mtl files to extract material parameters from each material,
// unique texture paths we need to load, and converts references to unique textures into indices in the array of unique
// texture paths that we created. This can be used to then load the final data we want in the next step
ccDataHandle ParseMaterialFile(const void* initialData, void* output, void* userData);

#endif //!CONTENT_COMPILER_OBJ_MATERIAL_HPP