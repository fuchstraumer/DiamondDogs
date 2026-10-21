#pragma once
#ifndef DIAMOND_DOGS_OCEAN_MANIFEST_DESCRIPTORS_HPP
#define DIAMOND_DOGS_OCEAN_MANIFEST_DESCRIPTORS_HPP
#include "ResourceTypes.hpp"
#include "VariantKey.hpp"
#include <vulkan/vulkan_core.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lodestone
{
    class ManifestIndex;
}

namespace ocean
{
    struct BoundResource
    {
        GraphicsResource Resource;
        /** @brief The layout a sampled image is in when the shader reads it. Storage images are always General */
        VkImageLayout SampledLayout{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    };

    using BoundResources = std::unordered_map<std::string_view, BoundResource>;

    /** @brief Writes every resource that the named entry points of `variant` read into `set` (set 0), by the names the
     *  cook reflected. The binding numbers and descriptor types come from the manifest, never from this code.
     *  @return An error message when an entry point reads a resource that `resources` doesn't name */
    [[nodiscard]] std::optional<std::string> WriteManifestDescriptors(
        VkDevice device,
        VkDescriptorSet set,
        const lodestone::ManifestIndex& index,
        lodestone::VariantKey variant,
        std::span<const std::string_view> entry_points,
        const BoundResources& resources);
}

#endif //!DIAMOND_DOGS_OCEAN_MANIFEST_DESCRIPTORS_HPP
