#include "VulkanMapping.hpp"
#include "RhiFlags.hpp"
#include "ShaderLibraryTypes.hpp"
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <optional>
#include <utility>

namespace loom
{

    VkShaderStageFlags ToVkShaderStages(lodestone::ShaderStageKind stages) noexcept
    {
        constexpr std::pair<lodestone::ShaderStageKind, VkShaderStageFlagBits> k_Table[]
        {
            { lodestone::ShaderStageKind::Vertex, VK_SHADER_STAGE_VERTEX_BIT },
            { lodestone::ShaderStageKind::Fragment, VK_SHADER_STAGE_FRAGMENT_BIT },
            { lodestone::ShaderStageKind::Compute, VK_SHADER_STAGE_COMPUTE_BIT },
            { lodestone::ShaderStageKind::Mesh, VK_SHADER_STAGE_MESH_BIT_EXT },
            { lodestone::ShaderStageKind::Task, VK_SHADER_STAGE_TASK_BIT_EXT },
        };

        VkShaderStageFlags result = 0u;

        for (const auto& [stage, vkStage] : k_Table)
        {
            if ((static_cast<uint32_t>(stages) & static_cast<uint32_t>(stage)) != 0u)
            {
                result |= vkStage;
            }
        }

        return result;
    }

    rhi::ShaderStageFlags ToRhiShaderStages(lodestone::ShaderStageKind stages) noexcept
    {
        constexpr std::pair<lodestone::ShaderStageKind, rhi::ShaderStageFlags> k_Table[]
        {
            { lodestone::ShaderStageKind::Vertex, rhi::ShaderStageFlags::Vertex },
            { lodestone::ShaderStageKind::Fragment, rhi::ShaderStageFlags::Fragment },
            { lodestone::ShaderStageKind::Compute, rhi::ShaderStageFlags::Compute },
            { lodestone::ShaderStageKind::Mesh, rhi::ShaderStageFlags::Mesh },
            { lodestone::ShaderStageKind::Task, rhi::ShaderStageFlags::Task },
        };

        rhi::ShaderStageFlags result = rhi::ShaderStageFlags::None;

        for (const auto& [stage, rhiStage] : k_Table)
        {
            if ((static_cast<uint32_t>(stages) & static_cast<uint32_t>(stage)) != 0u)
            {
                result |= rhiStage;
            }
        }

        return result;
    }

    std::optional<VkDescriptorType> ToDescriptorType(lodestone::BindingKind kind, bool writable) noexcept
    {
        switch (kind)
        {
        case lodestone::BindingKind::Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
        case lodestone::BindingKind::Texture:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case lodestone::BindingKind::UniformBuffer:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case lodestone::BindingKind::StorageBuffer:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case lodestone::BindingKind::TexelBuffer:
            return writable ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case lodestone::BindingKind::CombinedTextureSampler:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case lodestone::BindingKind::InputRenderTarget:
            return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        case lodestone::BindingKind::StorageTexture:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        default:
            break;
        }

        return std::nullopt;
    }

}
