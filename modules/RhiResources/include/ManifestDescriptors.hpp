#pragma once
#ifndef DIAMOND_DOGS_MANIFEST_DESCRIPTORS_HPP
#define DIAMOND_DOGS_MANIFEST_DESCRIPTORS_HPP
#include "ResourceTypes.hpp"
#include <cstddef>
#include "VariantKey.hpp"
#include <vulkan/vulkan_core.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lodestone
{
    class ManifestIndex;
}

struct BoundResource
{
    GraphicsResource Resource;
    /** @brief The layout a sampled image is in when the shader reads it. Storage images are always General */
    VkImageLayout SampledLayout{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    /** @brief For a binding that the manifest reflects as an array: one resource per element, exactly as many as
     *  the array has. Empty for a single binding, which uses Resource */
    std::vector<GraphicsResource> Elements;
};

using BoundResources = std::unordered_map<std::string_view, BoundResource>;

/** @brief Writes every resource that the named entry points of `variant` read into `set` (set 0), by the names the
 *  cook reflected. The binding numbers, descriptor types and array counts come from the manifest, never from this
 *  code.
 *  @return An error message when an entry point reads a resource that `resources` doesn't name, or an array binding
 *  gets a different number of elements than it has */
[[nodiscard]] std::optional<std::string> WriteManifestDescriptors(
    VkDevice device,
    VkDescriptorSet set,
    const lodestone::ManifestIndex& index,
    lodestone::VariantKey variant,
    std::span<const std::string_view> entry_points,
    const BoundResources& resources);

/** @brief Writes the 64-bit DeviceAddress of every PlacementKind::Pointer resource that the named entry points of
 *  `variant` read into `target_bytes` at resource.ValueOffset().
 *  @return An error message when an entry point reads a pointer resource that `resources` doesn't name, or when
 *  the offset exceeds `target_bytes`. */
[[nodiscard]] std::optional<std::string> WriteManifestPointerAddresses(
    std::span<std::byte> target_bytes,
    const lodestone::ManifestIndex& index,
    lodestone::VariantKey variant,
    std::span<const std::string_view> entry_points,
    const BoundResources& resources);

#endif //!DIAMOND_DOGS_MANIFEST_DESCRIPTORS_HPP
