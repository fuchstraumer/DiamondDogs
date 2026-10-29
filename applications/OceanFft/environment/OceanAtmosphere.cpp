#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "OceanAtmosphere.hpp"

#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "GpuTimers.hpp"
#include "ImageDataFormats.hpp"
#include "ManifestDescriptors.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifestIndex.hpp"
#include "VariantKey.hpp"
#include "math/Math.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace
{
    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    constexpr std::string_view k_ModuleName = "OceanAtmosphere";

    constexpr std::string_view k_TransmittanceEntry = "AtmosphereTransmittanceCS";
    constexpr std::string_view k_DirectIrradianceEntry = "AtmosphereDirectIrradianceCS";
    constexpr std::string_view k_SingleScatteringEntry = "AtmosphereSingleScatteringCS";
    constexpr std::string_view k_ScatteringDensityEntry = "AtmosphereScatteringDensityCS";
    constexpr std::string_view k_IndirectIrradianceEntry = "AtmosphereIndirectIrradianceCS";
    constexpr std::string_view k_MultipleScatteringEntry = "AtmosphereMultipleScatteringCS";

    // Set debug utility name for Vulkan objects
    void SetDebugName(rhi::Device& device, VkObjectType type, uint64_t handle, const char* name)
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
            info.objectType = type;
            info.objectHandle = handle;
            info.pObjectName = name;
            debugFns.vkSetDebugUtilsObjectName(device.Handle().As<VkDevice>(), &info);
        }

        (void)rhi::RhiSystem::SetObjectName(type, handle, name);
    }

    struct alignas(16) AtmospherePushConstants
    {
        float BottomRadiusKm{ 6360.0f };
        float TopRadiusKm{ 6420.0f };
        float RayleighScaleHeightKm{ 8.0f };
        float MieScaleHeightKm{ 1.2f };
        math::Float3 RayleighScattering{ 5.802e-3f, 13.558e-3f, 33.1e-3f };
        float MieAnisotropy{ 0.8f };
        math::Float3 MieScattering{ 3.996e-3f, 3.996e-3f, 3.996e-3f };
        float MieExtinction{ 4.440e-3f };
        math::Float3 OzoneAbsorption{ 0.650e-3f, 1.881e-3f, 0.085e-3f };
        float OzoneCenterKm{ 25.0f };
        float OzoneWidthKm{ 15.0f };
        float GroundAlbedo{ 0.1f };
        float Pad0[2]{ 0.0f, 0.0f };
        math::Float3 SolarIrradiance{ 1.0f, 1.0f, 1.0f };
        uint32_t ScatteringOrder{ 1u };
    };
    static_assert(sizeof(AtmospherePushConstants) == 96, "AtmospherePushConstants layout must match OceanAtmosphere.slang");
}

namespace ocean
{
    struct OceanAtmosphereImpl
    {
        ~OceanAtmosphereImpl();

        rhi::Device* Device{ nullptr };
        ResourceContext* Resources{ nullptr };
        const rhi::ShaderLibrary* Library{ nullptr };
        OceanAtmosphereSettings Settings{};
        bool IsPrecomputed{ false };

        // Output and intermediate LUTs
        GraphicsResource TransmittanceLutImage;
        GraphicsResource IrradianceLutImage;
        GraphicsResource DeltaIrradianceLutImage;
        GraphicsResource InscatterLutImage;
        GraphicsResource DeltaRayleighLutImage;
        GraphicsResource DeltaMieLutImage;
        GraphicsResource DeltaScatteringDensityLutImage;
        GraphicsResource DeltaMultipleScatteringLutImage;
        GraphicsResource LinearSampler;

        // Compute Programs
        std::unique_ptr<rhi::ManifestProgram> TransmittanceProgram;
        std::unique_ptr<rhi::ManifestProgram> DirectIrradianceProgram;
        std::unique_ptr<rhi::ManifestProgram> SingleScatteringProgram;
        std::unique_ptr<rhi::ManifestProgram> ScatteringDensityProgram;
        std::unique_ptr<rhi::ManifestProgram> IndirectIrradianceProgram;
        std::unique_ptr<rhi::ManifestProgram> MultipleScatteringProgram;

        const lodestone::ManifestIndex* Index{ nullptr };
        lodestone::VariantKey TransmittanceVariant{};
        lodestone::VariantKey DirectIrradianceVariant{};
        lodestone::VariantKey SingleScatteringVariant{};
        lodestone::VariantKey ScatteringDensityVariant{};
        lodestone::VariantKey IndirectIrradianceVariant{};
        lodestone::VariantKey MultipleScatteringVariant{};

        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        VkDescriptorSet TransmittanceSet{ VK_NULL_HANDLE };
        VkDescriptorSet DirectIrradianceSet{ VK_NULL_HANDLE };
        VkDescriptorSet SingleScatteringSet{ VK_NULL_HANDLE };
        VkDescriptorSet ScatteringDensitySet{ VK_NULL_HANDLE };
        VkDescriptorSet IndirectIrradianceSet{ VK_NULL_HANDLE };
        VkDescriptorSet MultipleScatteringSet{ VK_NULL_HANDLE };

        void DestroyGpuResources();
        [[nodiscard]] bool AllocateGpuTextures();
        [[nodiscard]] bool SetupDescriptorSets();
    };

    OceanAtmosphereImpl::~OceanAtmosphereImpl()
    {
        DestroyGpuResources();
    }

    void OceanAtmosphereImpl::DestroyGpuResources()
    {
        if (Device == nullptr)
        {
            return;
        }

        const VkDevice vkDev = Device->Handle().As<VkDevice>();

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(vkDev, DescriptorPool, nullptr);
            DescriptorPool = VK_NULL_HANDLE;
            TransmittanceSet = VK_NULL_HANDLE;
            DirectIrradianceSet = VK_NULL_HANDLE;
            SingleScatteringSet = VK_NULL_HANDLE;
            ScatteringDensitySet = VK_NULL_HANDLE;
            IndirectIrradianceSet = VK_NULL_HANDLE;
            MultipleScatteringSet = VK_NULL_HANDLE;
        }

        auto destroyResource = [this](GraphicsResource& res)
        {
            if (res)
            {
                (void)Resources->DestroyResource(res)->WaitForCompletion(k_TimeoutNs);
                res = GraphicsResource{};
            }
        };

        destroyResource(LinearSampler);
        destroyResource(DeltaMultipleScatteringLutImage);
        destroyResource(DeltaScatteringDensityLutImage);
        destroyResource(DeltaMieLutImage);
        destroyResource(DeltaRayleighLutImage);
        destroyResource(InscatterLutImage);
        destroyResource(DeltaIrradianceLutImage);
        destroyResource(IrradianceLutImage);
        destroyResource(TransmittanceLutImage);
    }

    bool OceanAtmosphereImpl::AllocateGpuTextures()
    {
        auto create2D = [this](uint32_t w, uint32_t h, const char* name) -> std::optional<GraphicsResource>
        {
            ImageCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Usage = ImageUsageBits::UavComputeShader | ImageUsageBits::SampledImage |
                         ImageUsageBits::SrvComputeShader | ImageUsageBits::SrvFragmentShader |
                         ImageUsageBits::TransferSource;
            info.Type = ImageType::Type2D;
            info.Format = CommonFormats::RGBA16_Float;
            info.Width = w;
            info.Height = h;
            info.Depth = 1u;
            info.MipLevels = 1u;
            info.ArrayLayers = 1u;
            info.Name = name;
            info.ViewInfo = ImageViewCreateInfo{};
            info.ViewInfo->ViewType = ImageViewType::Type2D;
            info.ViewInfo->Range.LevelCount = 1u;
            info.ViewInfo->Range.LayerCount = 1u;

            std::shared_ptr<GraphicsResourceReply> reply = Resources->CreateImage(info, {});
            if (reply == nullptr || reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                std::cerr << "OceanAtmosphere: failed to create 2D image " << name << "\n";
                return std::nullopt;
            }
            GraphicsResource res = reply->GetResource();
            SetDebugName(*Device, VK_OBJECT_TYPE_IMAGE, res.VkHandle, name);
            SetDebugName(*Device, VK_OBJECT_TYPE_IMAGE_VIEW, res.VkViewHandle, name);
            return res;
        };

        auto create3D = [this](uint32_t w, uint32_t h, uint32_t d, const char* name) -> std::optional<GraphicsResource>
        {
            ImageCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Usage = ImageUsageBits::UavComputeShader | ImageUsageBits::SampledImage |
                         ImageUsageBits::SrvComputeShader | ImageUsageBits::SrvFragmentShader |
                         ImageUsageBits::TransferSource;
            info.Type = ImageType::Type3D;
            info.Format = CommonFormats::RGBA16_Float;
            info.Width = w;
            info.Height = h;
            info.Depth = d;
            info.MipLevels = 1u;
            info.ArrayLayers = 1u;
            info.Name = name;
            info.ViewInfo = ImageViewCreateInfo{};
            info.ViewInfo->ViewType = ImageViewType::Type3D;
            info.ViewInfo->Range.LevelCount = 1u;
            info.ViewInfo->Range.LayerCount = 1u;

            std::shared_ptr<GraphicsResourceReply> reply = Resources->CreateImage(info, {});
            if (reply == nullptr || reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                std::cerr << "OceanAtmosphere: failed to create 3D image " << name << "\n";
                return std::nullopt;
            }
            GraphicsResource res = reply->GetResource();
            SetDebugName(*Device, VK_OBJECT_TYPE_IMAGE, res.VkHandle, name);
            SetDebugName(*Device, VK_OBJECT_TYPE_IMAGE_VIEW, res.VkViewHandle, name);
            return res;
        };

        // 1. Transmittance LUT (256x64)
        if (auto res = create2D(256u, 64u, "Ocean_AtmoTransmittanceLut"))
        {
            TransmittanceLutImage = *res;
        }
        else
        {
            return false;
        }

        // 2. Irradiance LUTs (64x16)
        if (auto res = create2D(64u, 16u, "Ocean_AtmoIrradianceLut"))
        {
            IrradianceLutImage = *res;
        }
        else
        {
            return false;
        }

        if (auto res = create2D(64u, 16u, "Ocean_AtmoDeltaIrradianceLut"))
        {
            DeltaIrradianceLutImage = *res;
        }
        else
        {
            return false;
        }

        // 3. 4D Inscatter LUTs packed into 3D (256x128x32)
        if (auto res = create3D(256u, 128u, 32u, "Ocean_AtmoInscatterLut"))
        {
            InscatterLutImage = *res;
        }
        else
        {
            return false;
        }

        if (auto res = create3D(256u, 128u, 32u, "Ocean_AtmoDeltaRayleighLut"))
        {
            DeltaRayleighLutImage = *res;
        }
        else
        {
            return false;
        }

        if (auto res = create3D(256u, 128u, 32u, "Ocean_AtmoDeltaMieLut"))
        {
            DeltaMieLutImage = *res;
        }
        else
        {
            return false;
        }

        if (auto res = create3D(256u, 128u, 32u, "Ocean_AtmoDeltaScatteringDensityLut"))
        {
            DeltaScatteringDensityLutImage = *res;
        }
        else
        {
            return false;
        }

        if (auto res = create3D(256u, 128u, 32u, "Ocean_AtmoDeltaMultipleScatteringLut"))
        {
            DeltaMultipleScatteringLutImage = *res;
        }
        else
        {
            return false;
        }

        // 4. Linear clamp sampler
        SamplerCreateInfo sampInfo{};
        sampInfo.MagFilter = ImageFilteringMode::Linear;
        sampInfo.MinFilter = ImageFilteringMode::Linear;
        sampInfo.MipFilter = ImageFilteringMode::Linear;
        sampInfo.AddressU = SamplerCreateInfo::AddressMode::ClampToEdge;
        sampInfo.AddressV = SamplerCreateInfo::AddressMode::ClampToEdge;
        sampInfo.AddressW = SamplerCreateInfo::AddressMode::ClampToEdge;
        sampInfo.MinLod = 0.0f;
        sampInfo.MaxLod = 0.0f;
        sampInfo.Name = "Ocean_AtmoSampler";

        std::shared_ptr<GraphicsResourceReply> sampReply = Resources->CreateSampler(sampInfo, "Ocean_AtmoSampler");
        if (sampReply == nullptr || sampReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            std::cerr << "OceanAtmosphere: failed to create sampler\n";
            return false;
        }
        LinearSampler = sampReply->GetResource();
        SetDebugName(*Device, VK_OBJECT_TYPE_SAMPLER, LinearSampler.VkSamplerHandle, "Ocean_AtmoSampler");

        return true;
    }

    bool OceanAtmosphereImpl::SetupDescriptorSets()
    {
        if (Device == nullptr || Index == nullptr)
        {
            return false;
        }

        const VkDevice vkDev = Device->Handle().As<VkDevice>();

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(vkDev, DescriptorPool, nullptr);
            DescriptorPool = VK_NULL_HANDLE;
        }

        constexpr uint32_t totalSets = 6u;
        const std::array<VkDescriptorPoolSize, 3> poolSizes{
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, totalSets * 4u },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, totalSets * 6u },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, totalSets }
        };

        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .maxSets = totalSets,
            .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
            .pPoolSizes = poolSizes.data()
        };

        if (vkCreateDescriptorPool(vkDev, &poolInfo, nullptr, &DescriptorPool) != VK_SUCCESS)
        {
            return false;
        }
        SetDebugName(*Device, VK_OBJECT_TYPE_DESCRIPTOR_POOL, reinterpret_cast<uint64_t>(DescriptorPool), "Ocean_AtmoDescriptorPool");

        auto allocateAndBindSet = [&](std::unique_ptr<rhi::ManifestProgram>& prog,
                                      lodestone::VariantKey variant,
                                      std::string_view entryName,
                                      const BoundResources& bound,
                                      VkDescriptorSet& outSet,
                                      const char* debugName) -> bool
        {
            if (prog == nullptr || prog->SetLayouts().empty())
            {
                return false;
            }

            const VkDescriptorSetLayout layout = prog->SetLayouts()[0].As<VkDescriptorSetLayout>();
            const VkDescriptorSetAllocateInfo allocInfo{
                .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = DescriptorPool,
                .descriptorSetCount = 1u,
                .pSetLayouts = &layout
            };

            if (vkAllocateDescriptorSets(vkDev, &allocInfo, &outSet) != VK_SUCCESS)
            {
                return false;
            }
            SetDebugName(*Device, VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(outSet), debugName);

            const std::array<std::string_view, 1> entryArray{ entryName };
            if (auto err = WriteManifestDescriptors(vkDev, outSet, *Index, variant, entryArray, bound); err.has_value())
            {
                std::cerr << "OceanAtmosphere " << debugName << " descriptors: " << *err << "\n";
                return false;
            }
            return true;
        };

        // 1. TransmittanceSet
        const BoundResources transBound{
            { "TransmittanceLutWrite", { TransmittanceLutImage } }
        };
        (void)allocateAndBindSet(TransmittanceProgram, TransmittanceVariant, k_TransmittanceEntry, transBound, TransmittanceSet, "Ocean_AtmoTransmittanceSet");

        // 2. DirectIrradianceSet
        const BoundResources directBound{
            { "TransmittanceLut", { TransmittanceLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "AtmosphereLinearSampler", { LinearSampler } },
            { "DeltaIrradianceLutWrite", { DeltaIrradianceLutImage } },
            { "IrradianceLutWrite", { IrradianceLutImage } }
        };
        (void)allocateAndBindSet(DirectIrradianceProgram, DirectIrradianceVariant, k_DirectIrradianceEntry, directBound, DirectIrradianceSet, "Ocean_AtmoDirectIrradianceSet");

        // 3. SingleScatteringSet
        const BoundResources singleBound{
            { "TransmittanceLut", { TransmittanceLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "AtmosphereLinearSampler", { LinearSampler } },
            { "DeltaRayleighLutWrite", { DeltaRayleighLutImage } },
            { "DeltaMieLutWrite", { DeltaMieLutImage } },
            { "InscatterLutWrite", { InscatterLutImage } }
        };
        (void)allocateAndBindSet(SingleScatteringProgram, SingleScatteringVariant, k_SingleScatteringEntry, singleBound, SingleScatteringSet, "Ocean_AtmoSingleScatteringSet");

        // 4. ScatteringDensitySet
        const BoundResources densityBound{
            { "TransmittanceLut", { TransmittanceLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "DeltaIrradianceLut", { DeltaIrradianceLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "DeltaRayleighLut", { DeltaRayleighLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "DeltaMieLut", { DeltaMieLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "DeltaMultipleScatteringLut", { DeltaMultipleScatteringLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "AtmosphereLinearSampler", { LinearSampler } },
            { "DeltaScatteringDensityLutWrite", { DeltaScatteringDensityLutImage } }
        };
        (void)allocateAndBindSet(ScatteringDensityProgram, ScatteringDensityVariant, k_ScatteringDensityEntry, densityBound, ScatteringDensitySet, "Ocean_AtmoScatteringDensitySet");

        // 5. IndirectIrradianceSet
        const BoundResources indirectBound{
            { "DeltaRayleighLut", { DeltaRayleighLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "DeltaMieLut", { DeltaMieLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "DeltaMultipleScatteringLut", { DeltaMultipleScatteringLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "AtmosphereLinearSampler", { LinearSampler } },
            { "DeltaIrradianceLutWrite", { DeltaIrradianceLutImage } },
            { "IrradianceLutWrite", { IrradianceLutImage } }
        };
        (void)allocateAndBindSet(IndirectIrradianceProgram, IndirectIrradianceVariant, k_IndirectIrradianceEntry, indirectBound, IndirectIrradianceSet, "Ocean_AtmoIndirectIrradianceSet");

        // 6. MultipleScatteringSet
        const BoundResources multiBound{
            { "TransmittanceLut", { TransmittanceLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "DeltaScatteringDensityLut", { DeltaScatteringDensityLutImage, VK_IMAGE_LAYOUT_GENERAL } },
            { "AtmosphereLinearSampler", { LinearSampler } },
            { "DeltaMultipleScatteringLutWrite", { DeltaMultipleScatteringLutImage } },
            { "InscatterLutWrite", { InscatterLutImage } }
        };
        (void)allocateAndBindSet(MultipleScatteringProgram, MultipleScatteringVariant, k_MultipleScatteringEntry, multiBound, MultipleScatteringSet, "Ocean_AtmoMultipleScatteringSet");

        return true;
    }

    std::expected<std::unique_ptr<OceanAtmosphere>, std::string> OceanAtmosphere::Create(
        rhi::Device& device,
        ResourceContext& resources,
        const rhi::ShaderLibrary& library,
        const OceanAtmosphereSettings& settings)
    {
        auto impl = std::make_unique<OceanAtmosphereImpl>();
        impl->Device = &device;
        impl->Resources = &resources;
        impl->Library = &library;
        impl->Settings = settings;

        if (!impl->AllocateGpuTextures())
        {
            return std::unexpected("OceanAtmosphere: failed to allocate GPU lookup textures");
        }

        auto moduleIndex = library.ModuleIndex(k_ModuleName);
        if (moduleIndex.has_value())
        {
            impl->Index = *moduleIndex;

            auto createProg = [&](std::string_view entry, std::unique_ptr<rhi::ManifestProgram>& prog, lodestone::VariantKey& varKey, const char* name)
            {
                const std::array<std::string_view, 1> entryArray{ entry };
                if (auto var = (*moduleIndex)->Query().SingleFor(entryArray); var.has_value())
                {
                    varKey = *var;
                    if (auto p = library.CreateProgram(device.Handle(), k_ModuleName, *var, entryArray); p.has_value())
                    {
                        prog = std::move(*p);
                        SetDebugName(device, VK_OBJECT_TYPE_PIPELINE_LAYOUT, prog->PipelineLayout().As<uint64_t>(), name);
                    }
                }
            };

            createProg(k_TransmittanceEntry, impl->TransmittanceProgram, impl->TransmittanceVariant, "Ocean_AtmoTransmittanceLayout");
            createProg(k_DirectIrradianceEntry, impl->DirectIrradianceProgram, impl->DirectIrradianceVariant, "Ocean_AtmoDirectIrradianceLayout");
            createProg(k_SingleScatteringEntry, impl->SingleScatteringProgram, impl->SingleScatteringVariant, "Ocean_AtmoSingleScatteringLayout");
            createProg(k_ScatteringDensityEntry, impl->ScatteringDensityProgram, impl->ScatteringDensityVariant, "Ocean_AtmoScatteringDensityLayout");
            createProg(k_IndirectIrradianceEntry, impl->IndirectIrradianceProgram, impl->IndirectIrradianceVariant, "Ocean_AtmoIndirectIrradianceLayout");
            createProg(k_MultipleScatteringEntry, impl->MultipleScatteringProgram, impl->MultipleScatteringVariant, "Ocean_AtmoMultipleScatteringLayout");

            (void)impl->SetupDescriptorSets();
        }

        return std::unique_ptr<OceanAtmosphere>(new OceanAtmosphere(std::move(impl)));
    }

    OceanAtmosphere::OceanAtmosphere(std::unique_ptr<OceanAtmosphereImpl>&& _impl) noexcept
        : impl{ std::move(_impl) }
    {}

    OceanAtmosphere::~OceanAtmosphere() = default;

    GraphicsResource OceanAtmosphere::TransmittanceLut() const noexcept
    {
        return impl->TransmittanceLutImage;
    }

    VkImageView OceanAtmosphere::TransmittanceLutView() const noexcept
    {
        return reinterpret_cast<VkImageView>(impl->TransmittanceLutImage.VkViewHandle);
    }

    GraphicsResource OceanAtmosphere::IrradianceLut() const noexcept
    {
        return impl->IrradianceLutImage;
    }

    VkImageView OceanAtmosphere::IrradianceLutView() const noexcept
    {
        return reinterpret_cast<VkImageView>(impl->IrradianceLutImage.VkViewHandle);
    }

    GraphicsResource OceanAtmosphere::InscatterLut() const noexcept
    {
        return impl->InscatterLutImage;
    }

    VkImageView OceanAtmosphere::InscatterLutView() const noexcept
    {
        return reinterpret_cast<VkImageView>(impl->InscatterLutImage.VkViewHandle);
    }

    GraphicsResource OceanAtmosphere::Sampler() const noexcept
    {
        return impl->LinearSampler;
    }

    VkSampler OceanAtmosphere::VkSamplerHandle() const noexcept
    {
        return reinterpret_cast<VkSampler>(impl->LinearSampler.VkSamplerHandle);
    }

    const OceanAtmosphereSettings& OceanAtmosphere::Settings() const noexcept
    {
        return impl->Settings;
    }

    bool OceanAtmosphere::IsPrecomputed() const noexcept
    {
        return impl->IsPrecomputed;
    }

    void OceanAtmosphere::RecordPrecompute(rhi::CommandBuffer& cmd, rhi::GpuTimerBlock* timer_block)
    {
        std::optional<rhi::ScopedGpuTimer> timer;
        if (timer_block != nullptr)
        {
            timer.emplace(*timer_block, "Ocean_AtmoPrecompute");
        }

        if (impl->TransmittanceProgram == nullptr ||
            impl->DirectIrradianceProgram == nullptr ||
            impl->SingleScatteringProgram == nullptr ||
            impl->ScatteringDensityProgram == nullptr ||
            impl->IndirectIrradianceProgram == nullptr ||
            impl->MultipleScatteringProgram == nullptr)
        {
            return;
        }

        AtmospherePushConstants push{
            .BottomRadiusKm = impl->Settings.BottomRadiusKm,
            .TopRadiusKm = impl->Settings.TopRadiusKm,
            .RayleighScaleHeightKm = impl->Settings.RayleighScaleHeightKm,
            .MieScaleHeightKm = impl->Settings.MieScaleHeightKm,
            .RayleighScattering = impl->Settings.RayleighScattering,
            .MieAnisotropy = impl->Settings.MieAnisotropy,
            .MieScattering = impl->Settings.MieScattering,
            .MieExtinction = impl->Settings.MieExtinction.x,
            .OzoneAbsorption = impl->Settings.OzoneAbsorption,
            .OzoneCenterKm = impl->Settings.OzoneCenterKm,
            .OzoneWidthKm = impl->Settings.OzoneWidthKm,
            .GroundAlbedo = impl->Settings.GroundAlbedo,
            .Pad0 = { 0.0f, 0.0f },
            .SolarIrradiance = impl->Settings.SolarIrradiance,
            .ScatteringOrder = 1u
        };

        auto pushConstants = [&](const rhi::ManifestProgram& prog)
        {
            if (const std::optional<rhi::PushConstantRange> range = prog.PushConstants(); range.has_value())
            {
                cmd.PushConstants(prog.PipelineLayout(), range->StageFlags, 0, std::as_bytes(std::span{ &push, 1 }));
            }
        };

        auto bindSet = [&](const rhi::ManifestProgram& prog, VkDescriptorSet set)
        {
            const rhi::DescriptorSetHandle setHandle{ reinterpret_cast<uint64_t>(set) };
            cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, prog.PipelineLayout(), 0, std::span{ &setHandle, 1 });
        };

        auto transitionImage = [&](const GraphicsResource& res, rhi::ImageLayout oldLayout, rhi::ImageLayout newLayout, rhi::Access srcAccess, rhi::Access dstAccess)
        {
            const rhi::ImageBarrier barrier{
                .Image = rhi::ImageHandle{ res.VkHandle },
                .SrcStages = rhi::PipelineStage::ComputeShader | rhi::PipelineStage::AllTransfer,
                .SrcAccess = srcAccess,
                .DstStages = rhi::PipelineStage::ComputeShader | rhi::PipelineStage::FragmentShader,
                .DstAccess = dstAccess,
                .OldLayout = oldLayout,
                .NewLayout = newLayout,
                .Range = rhi::ImageSubresourceRange{
                    .Aspects = rhi::ImageAspect::Color,
                    .BaseMipLevel = 0u,
                    .LevelCount = 1u,
                    .BaseArrayLayer = 0u,
                    .LayerCount = 1u
                }
            };
            cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &barrier, 1 } });
        };

        // -----------------------------------------------------------------------------------------
        transitionImage(impl->DeltaMultipleScatteringLutImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite);

        // Pass 1: AtmosphereTransmittanceCS (256x64)
        // -----------------------------------------------------------------------------------------
        transitionImage(impl->TransmittanceLutImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite);

        RhiAssert(cmd.BindShaders(impl->TransmittanceProgram->Program()));
        bindSet(*impl->TransmittanceProgram, impl->TransmittanceSet);
        pushConstants(*impl->TransmittanceProgram);
        cmd.Dispatch(32u, 8u, 1u); // 256/8, 64/8, 1

        transitionImage(impl->TransmittanceLutImage, rhi::ImageLayout::General, rhi::ImageLayout::General, rhi::Access::ShaderStorageWrite, rhi::Access::ShaderSampledRead);

        // -----------------------------------------------------------------------------------------
        // Pass 2: AtmosphereDirectIrradianceCS (64x16)
        // -----------------------------------------------------------------------------------------
        transitionImage(impl->DeltaIrradianceLutImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite);
        transitionImage(impl->IrradianceLutImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite);

        RhiAssert(cmd.BindShaders(impl->DirectIrradianceProgram->Program()));
        bindSet(*impl->DirectIrradianceProgram, impl->DirectIrradianceSet);
        pushConstants(*impl->DirectIrradianceProgram);
        cmd.Dispatch(8u, 2u, 1u); // 64/8, 16/8, 1

        // -----------------------------------------------------------------------------------------
        // Pass 3: AtmosphereSingleScatteringCS (256x128x32)
        // -----------------------------------------------------------------------------------------
        transitionImage(impl->DeltaRayleighLutImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite);
        transitionImage(impl->DeltaMieLutImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite);
        transitionImage(impl->InscatterLutImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite);

        RhiAssert(cmd.BindShaders(impl->SingleScatteringProgram->Program()));
        bindSet(*impl->SingleScatteringProgram, impl->SingleScatteringSet);
        pushConstants(*impl->SingleScatteringProgram);
        cmd.Dispatch(32u, 16u, 8u); // 256/8, 128/8, 32/4

        // -----------------------------------------------------------------------------------------
        // Passes 4..N: Higher-order multiple scattering iterations (k = 2..ScatteringOrders)
        // -----------------------------------------------------------------------------------------
        const uint32_t maxOrders = std::max(2u, impl->Settings.ScatteringOrders);
        for (uint32_t k = 2u; k <= maxOrders; ++k)
        {
            push.ScatteringOrder = k;

            // 4a: AtmosphereScatteringDensityCS (256x128x32)
            transitionImage(impl->DeltaIrradianceLutImage, rhi::ImageLayout::General, rhi::ImageLayout::General, rhi::Access::ShaderStorageWrite, rhi::Access::ShaderSampledRead);
            transitionImage(impl->DeltaRayleighLutImage, rhi::ImageLayout::General, rhi::ImageLayout::General, rhi::Access::ShaderStorageWrite, rhi::Access::ShaderSampledRead);
            transitionImage(impl->DeltaMieLutImage, rhi::ImageLayout::General, rhi::ImageLayout::General, rhi::Access::ShaderStorageWrite, rhi::Access::ShaderSampledRead);
            if (k > 2u)
            {
                transitionImage(impl->DeltaMultipleScatteringLutImage, rhi::ImageLayout::General, rhi::ImageLayout::General, rhi::Access::ShaderStorageWrite, rhi::Access::ShaderSampledRead);
            }
            transitionImage(impl->DeltaScatteringDensityLutImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite);

            RhiAssert(cmd.BindShaders(impl->ScatteringDensityProgram->Program()));
            bindSet(*impl->ScatteringDensityProgram, impl->ScatteringDensitySet);
            pushConstants(*impl->ScatteringDensityProgram);
            cmd.Dispatch(32u, 16u, 8u);

            // 4b: AtmosphereIndirectIrradianceCS (64x16)
            transitionImage(impl->DeltaIrradianceLutImage, rhi::ImageLayout::General, rhi::ImageLayout::General, rhi::Access::ShaderSampledRead, rhi::Access::ShaderStorageWrite);

            RhiAssert(cmd.BindShaders(impl->IndirectIrradianceProgram->Program()));
            bindSet(*impl->IndirectIrradianceProgram, impl->IndirectIrradianceSet);
            pushConstants(*impl->IndirectIrradianceProgram);
            cmd.Dispatch(8u, 2u, 1u);

            // 4c: AtmosphereMultipleScatteringCS (256x128x32)
            transitionImage(impl->DeltaScatteringDensityLutImage, rhi::ImageLayout::General, rhi::ImageLayout::General, rhi::Access::ShaderStorageWrite, rhi::Access::ShaderSampledRead);
            transitionImage(impl->DeltaMultipleScatteringLutImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::General, rhi::Access::None, rhi::Access::ShaderStorageWrite);

            RhiAssert(cmd.BindShaders(impl->MultipleScatteringProgram->Program()));
            bindSet(*impl->MultipleScatteringProgram, impl->MultipleScatteringSet);
            pushConstants(*impl->MultipleScatteringProgram);
            cmd.Dispatch(32u, 16u, 8u);
        }

        // Final barrier: transition all master LUTs to ShaderReadOnly layout
        transitionImage(impl->TransmittanceLutImage, rhi::ImageLayout::General, rhi::ImageLayout::ShaderReadOnly, rhi::Access::ShaderStorageWrite | rhi::Access::ShaderSampledRead, rhi::Access::ShaderSampledRead);
        transitionImage(impl->IrradianceLutImage, rhi::ImageLayout::General, rhi::ImageLayout::ShaderReadOnly, rhi::Access::ShaderStorageWrite, rhi::Access::ShaderSampledRead);
        transitionImage(impl->InscatterLutImage, rhi::ImageLayout::General, rhi::ImageLayout::ShaderReadOnly, rhi::Access::ShaderStorageWrite, rhi::Access::ShaderSampledRead);

        impl->IsPrecomputed = true;
    }
}
