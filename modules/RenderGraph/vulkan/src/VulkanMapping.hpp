#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_MAPPING_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_MAPPING_HPP
#include "RhiFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include <vulkan/vulkan_core.h>
#include <optional>

// VulkanMapping.cpp: Lodestone's neutral values in Vulkan and RHI terms. The barrier vocabulary needs no mapping: the
// core plans in rhi::PipelineStage and rhi::Access.
namespace loom
{

    [[nodiscard]] VkShaderStageFlags ToVkShaderStages(lodestone::ShaderStageKind stages) noexcept;
    [[nodiscard]] rhi::ShaderStageFlags ToRhiShaderStages(lodestone::ShaderStageKind stages) noexcept;
    // The descriptor type of a bound slot. Writable is true when the reflected access (CompiledData::SlotReflectedAccess) is not Read.
    [[nodiscard]] std::optional<VkDescriptorType> ToDescriptorType(lodestone::BindingKind kind, bool writable) noexcept;

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_MAPPING_HPP
