#include "SeabedMaterial.hpp"
#include "Device.hpp"
#include "ImageDataFormats.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include "RhiSystem.hpp"
#include "gli/format.hpp"
#include "gli/load.hpp"
#include "gli/texture2d.hpp"
#include "gli/type.hpp"
#include <filesystem>
#include <gli/gli.hpp>
#include <optional>
#include <string_view>
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;

    [[nodiscard]] std::optional<gli::texture2d> LoadKtx(const std::filesystem::path& path, gli::format expected_format)
    {
        std::error_code error;
        if (!std::filesystem::exists(path, error))
        {
            std::cerr << std::format("LoadSeabedMaterial: {} is missing\n", path.string());
            return std::nullopt;
        }

        const std::string pathText = path.string();
        gli::texture2d texture{ gli::load(pathText) };

        if (texture.empty())
        {
            std::cerr << std::format("LoadSeabedMaterial: {} is not a KTX file that gli can read\n", pathText);
            return std::nullopt;
        }

        if (texture.format() != expected_format)
        {
            std::cerr << std::format("LoadSeabedMaterial: {} has gli format {}, expected {}\n",
                pathText, static_cast<int32_t>(texture.format()), static_cast<int32_t>(expected_format));
            return std::nullopt;
        }

        const gli::extent2d extent = texture.extent(0u);
        const int32_t fullChain = static_cast<int32_t>(std::bit_width(static_cast<uint32_t>(std::max(extent.x, extent.y))));
        const int32_t levels = static_cast<int32_t>(texture.levels());

        if (levels != fullChain)
        {
            std::cerr << std::format("LoadSeabedMaterial: {} has {} mip levels, expected {}\n", pathText, levels, fullChain);
            return std::nullopt;
        }

        return texture;
    }

    [[nodiscard]] std::optional<GraphicsResource> CreateSampledImage(
        ResourceContext& resources,
        const gli::texture2d& texture,
        ImageFormat format,
        std::string_view name)
    {
        const uint32_t levels = static_cast<uint32_t>(texture.levels());
        const gli::extent2d extent = texture.extent(0u);

        std::vector<RhiImageResourceData> mipData;
        mipData.reserve(levels);

        for (uint32_t level = 0u; level < levels; ++level)
        {
            const gli::extent2d levelExtent = texture.extent(level);
            const std::byte* levelBytes = static_cast<const std::byte*>(texture.data(0u, 0u, level));

            RhiImageResourceData data{};
            data.Data = std::span<const std::byte>{ levelBytes, texture.size(level) };
            data.Width = static_cast<uint32_t>(levelExtent.x);
            data.Height = static_cast<uint32_t>(levelExtent.y);
            data.MipLevel = level;
            mipData.push_back(data);
        }

        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::SrvFragmentShader;
        info.Type = ImageType::Type2D;
        info.Format = format;
        info.Width = static_cast<uint32_t>(extent.x);
        info.Height = static_cast<uint32_t>(extent.y);
        info.Depth = 1u;
        info.MipLevels = levels;
        info.ViewInfo = ImageViewCreateInfo{};
        info.ViewInfo->ViewType = ImageViewType::Type2D;
        info.ViewInfo->Range.AspectMask = ImageAspectFlags::Color;
        info.ViewInfo->Range.LevelCount = levels;
        info.Name = name;

        std::shared_ptr<GraphicsResourceReply> reply = resources.CreateImage(info, mipData);
        if (reply == nullptr || reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            std::cerr << std::format("LoadSeabedMaterial: creating GPU image '{}' failed\n", name);
            return std::nullopt;
        }

        return reply->GetResource();
    }

    void SetDebugUtilsObjectName(rhi::Device& device, VkObjectType object_type, uint64_t handle, const char* name)
    {
        if (handle == 0u)
        {
            return;
        }

        const auto& debugFns = device.GetDebugUtilFns();
        if (debugFns.vkSetDebugUtilsObjectName != nullptr)
        {
            VkDebugUtilsObjectNameInfoEXT info{};
            info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            info.objectType = object_type;
            info.objectHandle = handle;
            info.pObjectName = name;
            debugFns.vkSetDebugUtilsObjectName(device.Handle().As<VkDevice>(), &info);
        }

        (void)rhi::RhiSystem::SetObjectName(object_type, handle, name);
    }
}

namespace ocean
{
    void SeabedMaterial::Destroy(ResourceContext& resources) noexcept
    {
        for (GraphicsResource* res : { &AlbedoImage, &OrdImage, &NormalImage, &Sampler })
        {
            if (*res)
            {
                (void)resources.DestroyResource(*res)->WaitForCompletion(k_TimeoutNs);
                *res = GraphicsResource{};
            }
        }
        AlbedoView = GraphicsResource{};
        OrdView = GraphicsResource{};
        NormalView = GraphicsResource{};
        Size = 0u;
    }

    std::optional<SeabedMaterial> LoadSeabedMaterial(
        const std::filesystem::path& folder,
        std::string_view name,
        ResourceContext& resources,
        rhi::Device& device)
    {
        const std::filesystem::path albedoPath = folder / std::format("{}_Albedo.ktx", name);
        const std::filesystem::path ordPath = folder / std::format("{}_Ord.ktx", name);
        const std::filesystem::path normalPath = folder / std::format("{}_Normal.ktx", name);

        std::optional<gli::texture2d> albedoTexture = LoadKtx(albedoPath, gli::FORMAT_RGBA8_UNORM_PACK8);
        if (!albedoTexture.has_value())
        {
            return std::nullopt;
        }

        std::optional<gli::texture2d> ordTexture = LoadKtx(ordPath, gli::FORMAT_RGBA8_UNORM_PACK8);
        if (!ordTexture.has_value())
        {
            return std::nullopt;
        }

        std::optional<gli::texture2d> normalTexture = LoadKtx(normalPath, gli::FORMAT_RG8_UNORM_PACK8);
        if (!normalTexture.has_value())
        {
            return std::nullopt;
        }

        std::optional<GraphicsResource> albedo =
            CreateSampledImage(resources, *albedoTexture, CommonFormats::RGBA8_UNorm, "Ocean_SeabedAlbedo");
        if (!albedo.has_value())
        {
            return std::nullopt;
        }

        std::optional<GraphicsResource> ord =
            CreateSampledImage(resources, *ordTexture, CommonFormats::RGBA8_UNorm, "Ocean_SeabedOrd");
        if (!ord.has_value())
        {
            (void)resources.DestroyResource(*albedo)->WaitForCompletion(k_TimeoutNs);
            return std::nullopt;
        }

        std::optional<GraphicsResource> normal =
            CreateSampledImage(resources, *normalTexture, CommonFormats::RG8_UNorm, "Ocean_SeabedNormal");
        if (!normal.has_value())
        {
            (void)resources.DestroyResource(*albedo)->WaitForCompletion(k_TimeoutNs);
            (void)resources.DestroyResource(*ord)->WaitForCompletion(k_TimeoutNs);
            return std::nullopt;
        }

        SamplerCreateInfo samplerInfo{};
        samplerInfo.MagFilter = ImageFilteringMode::Linear;
        samplerInfo.MinFilter = ImageFilteringMode::Linear;
        samplerInfo.MipFilter = ImageFilteringMode::Linear;
        samplerInfo.AddressU = SamplerCreateInfo::AddressMode::Repeat;
        samplerInfo.AddressV = SamplerCreateInfo::AddressMode::Repeat;
        samplerInfo.AddressW = SamplerCreateInfo::AddressMode::Repeat;

        VkPhysicalDevice physicalDevice = device.GetPhysicalDevice().As<VkPhysicalDevice>();
        if (physicalDevice != VK_NULL_HANDLE)
        {
            VkPhysicalDeviceFeatures features{};
            vkGetPhysicalDeviceFeatures(physicalDevice, &features);
            if (features.samplerAnisotropy == VK_TRUE)
            {
                VkPhysicalDeviceProperties properties{};
                vkGetPhysicalDeviceProperties(physicalDevice, &properties);
                samplerInfo.AnisotropyEnabled = true;
                samplerInfo.MaxAnisotropy = std::min(8.0f, properties.limits.maxSamplerAnisotropy);
            }
        }

        std::shared_ptr<GraphicsResourceReply> samplerReply = resources.CreateSampler(samplerInfo, "Ocean_SeabedSampler");
        if (samplerReply == nullptr || samplerReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            std::cerr << "LoadSeabedMaterial: creating GPU sampler failed\n";
            (void)resources.DestroyResource(*albedo)->WaitForCompletion(k_TimeoutNs);
            (void)resources.DestroyResource(*ord)->WaitForCompletion(k_TimeoutNs);
            (void)resources.DestroyResource(*normal)->WaitForCompletion(k_TimeoutNs);
            return std::nullopt;
        }

        GraphicsResource sampler = samplerReply->GetResource();

        // Set debug names on all created Vulkan resources
        SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE, albedo->VkHandle, "Ocean_SeabedAlbedo");
        SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, albedo->VkViewHandle, "Ocean_SeabedAlbedo");
        SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE, ord->VkHandle, "Ocean_SeabedOrd");
        SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, ord->VkViewHandle, "Ocean_SeabedOrd");
        SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE, normal->VkHandle, "Ocean_SeabedNormal");
        SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, normal->VkViewHandle, "Ocean_SeabedNormal");
        SetDebugUtilsObjectName(device, VK_OBJECT_TYPE_SAMPLER, sampler.VkSamplerHandle, "Ocean_SeabedSampler");

        SeabedMaterial material{};
        material.AlbedoImage = *albedo;
        material.AlbedoView = *albedo;
        material.AlbedoView.Type = ResourceType::ImageView;
        material.AlbedoView.VkHandle = albedo->VkViewHandle;

        material.OrdImage = *ord;
        material.OrdView = *ord;
        material.OrdView.Type = ResourceType::ImageView;
        material.OrdView.VkHandle = ord->VkViewHandle;

        material.NormalImage = *normal;
        material.NormalView = *normal;
        material.NormalView.Type = ResourceType::ImageView;
        material.NormalView.VkHandle = normal->VkViewHandle;

        material.Sampler = sampler;
        material.Size = static_cast<uint32_t>(albedoTexture->extent(0u).x);

        return material;
    }
}
