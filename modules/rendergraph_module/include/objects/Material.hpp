#pragma once
#ifndef DIAMOND_DOGS_MATERIAL_RESOURCE_HPP
#define DIAMOND_DOGS_MATERIAL_RESOURCE_HPP
#include "MaterialParameters.hpp"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>
#include <memory>
#include "glm/vec4.hpp"


struct VulkanResource;
    
struct material_textures_t {
    VulkanResource* Albedo{ nullptr };
    VulkanResource* Normal{ nullptr };
    VulkanResource* AmbientOcclusion{ nullptr };
    VulkanResource* Roughness{ nullptr };
    VulkanResource* Metallic{ nullptr };
    VulkanResource* Emissive{ nullptr };
    bool operator==(const material_textures_t& other) const noexcept {
        return (Albedo == other.Albedo) && (Normal == other.Normal) &&
            (AmbientOcclusion == other.AmbientOcclusion) && (Roughness == other.Roughness) &&
            (Metallic == other.Metallic) && (Emissive == other.Emissive);
    }
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

#endif //!DIAMOND_DOGS_MATERIAL_RESOURCE_HPP
