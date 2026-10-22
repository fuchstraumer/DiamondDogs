#include "ManifestDescriptors.hpp"
#include "ResourceTypes.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderManifestLayout.hpp"
#include "VariantKey.hpp"
#include "vulkan/vulkan_core.h"
#include <cstdint>
#include <deque>
#include <format>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    // each write points at its own array of infos. Moving a vector into the deque keeps its storage where it is, and
    // a deque never moves its elements when it grows
    struct DescriptorInfos
    {
        std::deque<std::vector<VkDescriptorBufferInfo>> Buffers;
        std::deque<std::vector<VkDescriptorImageInfo>> Images;
    };

    // sets the write's type and its infos, one per element
    [[nodiscard]] bool SetWriteElements(lodestone::BindingKind kind, std::span<const GraphicsResource> elements, VkImageLayout sampled_layout,
        DescriptorInfos& infos, VkWriteDescriptorSet& write)
    {
        switch (kind)
        {
        case lodestone::BindingKind::StorageBuffer:
        case lodestone::BindingKind::UniformBuffer:
        {
            write.descriptorType = kind == lodestone::BindingKind::StorageBuffer ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            std::vector<VkDescriptorBufferInfo>& buffers = infos.Buffers.emplace_back();

            for (const GraphicsResource& element : elements)
            {
                buffers.push_back(VkDescriptorBufferInfo{ reinterpret_cast<VkBuffer>(element.VkHandle), 0, VK_WHOLE_SIZE });
            }

            write.pBufferInfo = buffers.data();
            return true;
        }
        case lodestone::BindingKind::Texture:
        case lodestone::BindingKind::StorageTexture:
        case lodestone::BindingKind::Sampler:
        {
            const bool storage = kind == lodestone::BindingKind::StorageTexture;
            const bool sampler = kind == lodestone::BindingKind::Sampler;
            write.descriptorType = storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : sampler ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            std::vector<VkDescriptorImageInfo>& images = infos.Images.emplace_back();

            for (const GraphicsResource& element : elements)
            {
                if (sampler)
                {
                    images.push_back(VkDescriptorImageInfo{ reinterpret_cast<VkSampler>(element.VkSamplerHandle), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED });
                }
                else
                {
                    images.push_back(VkDescriptorImageInfo{ VK_NULL_HANDLE, reinterpret_cast<VkImageView>(element.VkViewHandle), storage ? VK_IMAGE_LAYOUT_GENERAL : sampled_layout });
                }
            }

            write.pImageInfo = images.data();
            return true;
        }
        default:
            return false;
        }
    }
}

std::optional<std::string> WriteManifestDescriptors(
    VkDevice device,
    VkDescriptorSet set,
    const lodestone::ManifestIndex& index,
    lodestone::VariantKey variant,
    std::span<const std::string_view> entry_points,
    const BoundResources& resources)
{
    const lodestone::manifest::EnvironmentView& environment = index.View();
    std::optional<lodestone::manifest::VariantView> variantView = environment.VariantByKey(variant);
    if (!variantView.has_value())
    {
        return std::string{ "the variant is not in the environment" };
    }

    DescriptorInfos infos;
    std::vector<VkWriteDescriptorSet> writes;
    // two stages that read one resource list it twice: write each binding once
    std::set<uint32_t> written;

    for (const std::string_view entryPoint : entry_points)
    {
        lodestone::manifest::ManifestResult<uint32_t> entryPointIndex = environment.Module().FindEntryPointIndex(entryPoint);
        if (!entryPointIndex.has_value())
        {
            return std::format("{} is not in the module", entryPoint);
        }

        for (const lodestone::manifest::ResolvedResource resource : variantView->EntryPoint(*entryPointIndex).Layout())
        {
            const uint32_t binding = resource.PlacementValue().Word1;
            if (!written.insert(binding).second)
            {
                continue;
            }

            const auto found = resources.find(resource.Name());
            if (found == resources.end())
            {
                return std::format("{} reads {}, which this host does not provide", entryPoint, resource.Name());
            }

            // an array binding takes Elements, one per element; a single binding takes Resource
            const BoundResource& bound = found->second;
            const uint32_t arrayCount = resource.ArrayCount();
            const bool isArray = arrayCount > 1u;
            const std::span<const GraphicsResource> elements = isArray ? std::span{ bound.Elements } : std::span{ &bound.Resource, 1 };

            if (elements.size() != arrayCount)
            {
                return std::format("{}: {} is an array of {}, and the host gave {} elements", entryPoint, resource.Name(), arrayCount, elements.size());
            }

            VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, 0, arrayCount };

            if (!SetWriteElements(resource.Kind(), elements, bound.SampledLayout, infos, write))
            {
                return std::format("{}: {} has a binding kind this host does not handle", entryPoint, resource.Name());
            }

            writes.push_back(write);
        }
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    return std::nullopt;
}
