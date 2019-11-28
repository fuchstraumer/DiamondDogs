#pragma once
#ifndef CONTENT_COMPILER_OBJ_MATERIAL_HPP
#define CONTENT_COMPILER_OBJ_MATERIAL_HPP
#include "MeshData.hpp"

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
    uint32_t ambient{ 0u };
    uint32_t diffuse{ 0u };
    uint32_t specular{ 0u };
    uint32_t specularHighlight{ 0u };
    uint32_t bump{ 0u };
    uint32_t displacement{ 0u };
    uint32_t alpha{ 0u };
    uint32_t reflection{ 0u };
    // PBR extensions
    uint32_t roughness{ 0u };
    uint32_t metallic{ 0u };
    uint32_t sheen{ 0u };
    uint32_t emissive{ 0u };
    uint32_t normal{ 0u };
};

struct ObjMaterial
{
    ObjMaterialData data;
    ObjMaterialTextures textures;
};

struct ObjMaterialFile
{
    size_t numMaterials{ 0u };
    ObjMaterial* materials{ nullptr };
    size_t numTextures{ 0u };
    ccDataHandle* textures{ nullptr };
};

ccDataHandle LoadMaterialFile(const char* fname, const char* directory = nullptr);

#endif //!CONTENT_COMPILER_OBJ_MATERIAL_HPP