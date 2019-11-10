#pragma once
#ifndef DIAMOND_DOGS_MATERIAL_STRUCTURES_HPP
#define DIAMOND_DOGS_MATERIAL_STRUCTURES_HPP
#include <cstdint>

enum class texture_type : uint8_t
{
    Invalid = 0u,
    Ambient,
    Diffuse,
    Specular,
    SpecularHighlight,
    Bump,
    Displacement,
    Alpha,
    Roughness,
    Metallic,
    Sheen,
    Emissive,
    Normal,
    // not intended for use anywhere but internally
    // used to store bindings, kinda needed
    ParametersUBO,
    Count
};

struct material_shader_indices_t
{
    /*
        We use signed ints so we can use -1 as a sentinel
        value: either means stuff is loading, or the curr
        material just doesn't use that texture type
    */
    int32_t ParamsIdx{ -1 };
    int32_t AlbedoIdx{ -1 };
    int32_t AlphaIdx{ -1 };
    int32_t SpecularIdx{ -1 };
    int32_t BumpMapIdx{ -1 };
    int32_t DisplacementMapIdx{ -1 };
    int32_t NormalMapIdx{ -1 };
    int32_t AoMapIdx{ -1 };
    int32_t RoughnessMapIdx{ -1 };
    int32_t MetallicMapIdx{ -1 };
    int32_t EmissiveMapIdx{ -1 };
};

struct material_parameters_t
{
    float ambient[3]{ 0.0f, 0.0f, 0.0f };
    float diffuse[3]{ 0.0f, 0.0f, 0.0f };
    float specular[3]{ 0.0f, 0.0f, 0.0f };
    float transmittance[3]{ 0.0f, 0.0f, 0.0f };
    float emission[3]{ 0.0f, 0.0f, 0.0f };
    float shininess{ 0.01f };
    float ior{ 0.0f };
    float alpha{ 0.0f };
    int32_t illum{ 0 }; // illumination model. not currently used, but could be in the future
    float roughness{ 0.1f };
    float metallic{ 0.0f };
    float sheen{ 0.0f };
    float clearcoat_thickness{ 0.0f };
    float clearcoat_roughness{ 0.0f };
    float anisotropy{ 0.0f };
    float anisotropy_rotation{ 0.0f };
};

struct material_texture_toggles_t
{
    bool hasAlbedoMap{ false };
    bool hasAlphaMap{ false };
    bool hasSpecularMap{ false };
    bool hasBumpMap{ false };
    bool hasDisplacementMap{ false };
    bool hasNormalMap{ false };
    bool hasAmbientOcclusionMap{ false };
    bool hasMetallicMap{ false };
    bool hasRoughnessMap{ false };
    bool hasEmissiveMap{ false };
};

struct VulkanResource;

struct material_vulkan_resources_t
{
    VulkanResource* paramsUbo{ nullptr };
    VulkanResource* albedoMap{ nullptr };
    VulkanResource* alphaMap{ nullptr };
    VulkanResource* specularMap{ nullptr };
    VulkanResource* bumpMap{ nullptr };
    VulkanResource* displacementMap{ nullptr };
    VulkanResource* normalMap{ nullptr };
    VulkanResource* ambientOcclusionMap{ nullptr };
    VulkanResource* metallicMap{ nullptr };
    VulkanResource* roughnessMap{ nullptr };
    VulkanResource* emissiveMap{ nullptr };
};

struct MaterialCreateInfo
{
    // Set based on maaxPerStageDescriptorSampledImages minimum, on vulkan.gpuinfo.org
    // I'm using it mostly because I expect exceeding this, even in a bindless mode, to have perf implications
    constexpr static size_t MaxNumTextures = 16u;
    const char* MaterialName{ nullptr };
    const char* MaterialDirectory{ nullptr };
    size_t NumTextures{ 0u };
    texture_type Types[MaxNumTextures];
    const char* TexturePaths[MaxNumTextures];
    material_parameters_t Parameters;
};

#endif //!DIAMOND_DOGS_MATERIAL_STRUCTURES_HPP
