#include "ManifestDescriptors.hpp"
#include "ResourceTypes.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderManifestLayout.hpp"
#include "VariantKey.hpp"
#include "vulkan/vulkan_core.h"
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ocean
{
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

        // the writes point into these, so they are reserved up front and never grow past it
        constexpr size_t k_MaxWrites = 32u;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkDescriptorImageInfo> imageInfos;
        bufferInfos.reserve(k_MaxWrites);
        imageInfos.reserve(k_MaxWrites);
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

                if (writes.size() == k_MaxWrites)
                {
                    return std::string{ "too many bindings for one set" };
                }

                const GraphicsResource& bound = found->second.Resource;
                VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, binding, 0, 1 };
                switch (resource.Kind())
                {
                case lodestone::BindingKind::StorageBuffer:
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    write.pBufferInfo = &bufferInfos.emplace_back(VkDescriptorBufferInfo{ reinterpret_cast<VkBuffer>(bound.VkHandle), 0, VK_WHOLE_SIZE });
                    break;
                case lodestone::BindingKind::UniformBuffer:
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    write.pBufferInfo = &bufferInfos.emplace_back(VkDescriptorBufferInfo{ reinterpret_cast<VkBuffer>(bound.VkHandle), 0, VK_WHOLE_SIZE });
                    break;
                case lodestone::BindingKind::Texture:
                    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    write.pImageInfo = &imageInfos.emplace_back(VkDescriptorImageInfo{ VK_NULL_HANDLE, reinterpret_cast<VkImageView>(bound.VkViewHandle), found->second.SampledLayout });
                    break;
                case lodestone::BindingKind::Sampler:
                    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                    write.pImageInfo = &imageInfos.emplace_back(VkDescriptorImageInfo{ reinterpret_cast<VkSampler>(bound.VkSamplerHandle), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED });
                    break;
                case lodestone::BindingKind::StorageTexture:
                    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    write.pImageInfo = &imageInfos.emplace_back(VkDescriptorImageInfo{ VK_NULL_HANDLE, reinterpret_cast<VkImageView>(bound.VkViewHandle), VK_IMAGE_LAYOUT_GENERAL });
                    break;
                default:
                    return std::format("{}: {} has a binding kind this host does not handle", entryPoint, resource.Name());
                }

                writes.push_back(write);
            }
        }

        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        return std::nullopt;
    }
}
