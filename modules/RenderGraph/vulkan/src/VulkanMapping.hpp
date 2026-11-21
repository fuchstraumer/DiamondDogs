#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_MAPPING_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_MAPPING_HPP
#include "FramePlan.hpp"
#include "RhiFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include <vulkan/vulkan_core.h>
#include <optional>

// VulkanMapping.cpp: Loom's and Lodestone's neutral values in Vulkan and RHI terms.
namespace loom
{

    [[nodiscard]] rhi::PipelineStage ToPipelineStages(SyncStage stages) noexcept;
    [[nodiscard]] rhi::Access ToAccess(SyncAccess accesses) noexcept;
    [[nodiscard]] VkShaderStageFlags ToVkShaderStages(lodestone::ShaderStageKind stages) noexcept;
    [[nodiscard]] rhi::ShaderStageFlags ToRhiShaderStages(lodestone::ShaderStageKind stages) noexcept;
    // The descriptor type of a bound slot. Writable is the reflected access (CompiledData::SlotWritable).
    [[nodiscard]] std::optional<VkDescriptorType> ToDescriptorType(lodestone::BindingKind kind, bool writable) noexcept;

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_MAPPING_HPP
