#pragma once
#ifndef DIAMOND_DOGS_MATERIAL_RESOURCE_HPP
#define DIAMOND_DOGS_MATERIAL_RESOURCE_HPP
#include "ForwardDecl.hpp"
#include "MaterialParameters.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <memory>
#include "glm/vec4.hpp"


struct VulkanResource;

enum class TextureType {
    Albedo = 0,
    Normal = 1,
    MetallicRoughness = 2,
    Occlusion = 3,
    Emissive = 4,
    Count = 5
};

struct material_texture_flags_t {
    // Using VkBool32 as we may end up directly using
    // this struct as part of our data pointer
    VkBool32 Albedo{ VK_FALSE };
    VkBool32 Normal{ VK_FALSE };
    VkBool32 AmbientOcclusion{ VK_FALSE };
    VkBool32 Roughness{ VK_FALSE };
    VkBool32 Metallic{ VK_FALSE };
    VkBool32 Emissive{ VK_FALSE };
    bool operator==(const material_texture_flags_t& other) const noexcept {
        return (Albedo == other.Albedo) && (Normal == other.Normal) &&
            (AmbientOcclusion == other.AmbientOcclusion) && (Roughness == other.Roughness) &&
            (Metallic == other.Metallic) && (Emissive == other.Emissive);
    }
};

class Material {
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;
public:

    Material(std::string _name);
    ~Material();

    // current_idx used as "offset"
    void Bind(VkCommandBuffer cmd, size_t current_idx);

private:

    std::string name;
    uint64_t hash;
    std::shared_ptr<vpr::DescriptorSet> descriptorSet;
    VulkanResource* textures[static_cast<size_t>(TextureType::Count)];
    MaterialParameters params;
};

#endif //!DIAMOND_DOGS_MATERIAL_RESOURCE_HPP
