#pragma once
#ifndef DIAMOND_DOGS_MATERIAL_STRUCTURES_HPP
#define DIAMOND_DOGS_MATERIAL_STRUCTURES_HPP
#include <cstdint>
#pragma warning(push, 1)
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#pragma warning(pop)

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
    ParametersUBO
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
    glm::vec3 ambient{ 0.0f,0.0f,0.0f };
    glm::vec3 diffuse{ 0.0f,0.0f,0.0f };
    glm::vec3 specular{ 0.0f,0.0f,0.0f };
    glm::vec3 transmittance{ 0.0f,0.0f,0.0f };
    glm::vec3 emission{ 0.0f,0.0f,0.0f };
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
    uint32_t hasAlbedoMap{ 0u };
    uint32_t hasAlphaMap{ 0u };
    uint32_t hasSpecularMap{ 0u };
    uint32_t hasBumpMap{ 0u };
    uint32_t hasDisplacementMap{ 0u };
    uint32_t hasNormalMap{ 0u };
    uint32_t hasAmbientOcclusionMap{ 0u };
    uint32_t hasMetallicMap{ 0u };
    uint32_t hasRoughnessMap{ 0u };
    uint32_t hasEmissiveMap{ 0u };
    uint32_t materialLoading{ 0u };
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

#endif //!DIAMOND_DOGS_MATERIAL_STRUCTURES_HPP
