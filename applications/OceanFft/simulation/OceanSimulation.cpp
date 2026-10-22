#include "OceanSimulation.hpp"
#include "DonelanBannerNormLUT.hpp"
#include "ImageDataFormats.hpp"
#include "ManifestDescriptors.hpp"
#include "CommandBuffer.hpp"
#include "Device.hpp"
#include "GpuTimers.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderManifestLayout.hpp"
#include "ShaderProgram.hpp"
#include "VariantKey.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <numbers>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace ocean
{
    namespace
    {
        constexpr std::string_view k_ModuleName{ "OceanFft" };
        constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;

        // the push constant blocks of OceanFftCommon.slang, OceanTick.slang and OceanResolve.slang
        struct IfftPushConstants
        {
            uint32_t Transpose{ 0u };
            uint32_t WaveSize{ 0u };
            uint32_t Log4WaveSize{ 0u };
            uint32_t Pad0{ 0u };
        };

        struct TickPushConstants
        {
            float Time{ 0.0f };
        };
        
        struct ResolvePushConstants
        {
            float Choppiness{ 1.0f };
        };

        struct MipsPushConstants
        {
            uint32_t SourceSize{ 0u };
            uint32_t LevelCount{ 0u };
        };

        // OceanMips.slang: one dispatch reads one level and writes up to five below it, from 32x32 source tiles
        constexpr uint32_t k_MipsLevelsPerDispatch = 5u;
        constexpr uint32_t k_MipsSourceTile = 32u;
        constexpr std::array<std::string_view, k_MipsLevelsPerDispatch> k_MipTargetNames
        {
            "MipTarget0", "MipTarget1", "MipTarget2", "MipTarget3", "MipTarget4"
        };
        // the first dispatch reads mip 0, the second reads mip 5. That covers every level up to size 1024 (11 levels)
        constexpr uint32_t k_MipsDispatches = 2u;
        constexpr uint32_t k_OutputImageCount = 2u;

        enum class Pass : uint8_t
        {
            SpectrumInit,
            SpectrumConjugate,
            Tick,
            IfftRadix2,
            IfftRadix4,
            Resolve,
            Mips,
            Count
        };
        constexpr std::array<std::string_view, static_cast<size_t>(Pass::Count)> k_EntryPoints
        {
            "SpectrumInitCS", "SpectrumConjugateCS", "OceanTickCS", "IfftRadix2CS", "IfftRadix4CS", "OceanResolveCS", "OceanMipsCS"
        };

        struct PassProgram
        {
            std::unique_ptr<rhi::ManifestProgram> Program;
            VkDescriptorSet Set{ VK_NULL_HANDLE };
        };

        [[nodiscard]] bool WaitForReply(const std::shared_ptr<GraphicsResourceReply>& reply) noexcept
        {
            return reply != nullptr && reply->WaitForCompletion(k_TimeoutNs) == MessageReply::Status::Completed;
        }

        void ComputeBarrier(rhi::CommandBuffer& command_buffer)
        {
            const rhi::GlobalBarrier barrier
            {
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageRead | rhi::Access::ShaderStorageWrite
            };
            command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &barrier, 1 }, {}, {} });
        }

        // OceanMipsCS samples the level that the dispatch before it wrote as a storage image
        void StorageToSampledBarrier(rhi::CommandBuffer& command_buffer)
        {
            const rhi::GlobalBarrier barrier
            {
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead | rhi::Access::ShaderStorageWrite
            };
            command_buffer.Barrier(rhi::DependencyInfo{ std::span{ &barrier, 1 }, {}, {} });
        }

        template<typename T>
        void PushConstants(rhi::CommandBuffer& command_buffer, const rhi::ManifestProgram& program, const T& value)
        {
            const std::optional<rhi::PushConstantRange> range = program.PushConstants();
            if (range.has_value())
            {
                command_buffer.PushConstants(program.PipelineLayout(), range->StageFlags, 0u,
                    std::as_bytes(std::span{ &value, 1 }).first(std::min<size_t>(sizeof(T), range->Size)));
            }
        }
    }

    struct OceanSimulationImpl
    {
        ~OceanSimulationImpl();

        rhi::Device* Device{ nullptr };
        ResourceContext* Resources{ nullptr };
        OceanSimulationSettings Settings;

        std::array<PassProgram, static_cast<size_t>(Pass::Count)> Programs;
        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };

        std::array<GraphicsResource, 4> Buffers{};
        std::array<uint64_t, 4> BufferBytes{};
        GraphicsResource CascadeParamsBuffer;
        GraphicsResource SpectrumParamsBuffer;
        GraphicsResource NormalizationLut;
        GraphicsResource DisplacementImage;
        GraphicsResource DerivativesImage;
        uint32_t MipLevels{ 1u };
        // OceanMipsCS reads and writes single levels, so it needs a view of each one. ResourceContext makes one view
        // per image, so these are owned here. Indexed [image][level], image 0 is Displacement
        std::array<std::vector<VkImageView>, k_OutputImageCount> MipViews;
        // [image][dispatch]
        std::array<std::array<VkDescriptorSet, k_MipsDispatches>, k_OutputImageCount> MipSets{};

        [[nodiscard]] const PassProgram& Program(Pass pass) const noexcept
        {
            return Programs[static_cast<size_t>(pass)];
        }

        void Bind(rhi::CommandBuffer& command_buffer, Pass pass) const
        {
            const PassProgram& passProgram = Program(pass);
            RhiAssert(command_buffer.BindShaders(passProgram.Program->Program()));
            const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(passProgram.Set) };
            command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, passProgram.Program->PipelineLayout(), 0, std::span{ &set, 1 });
        }

        [[nodiscard]] uint32_t SpectrumGroups() const noexcept
        {
            return Settings.Size / 8u;
        }

        // fills mips 1 and up of both outputs from mip 0. The first dispatch per image reads mip 0 and writes up to five
        // levels; the second reads the last of those and writes the rest. Each texel averages the 2x2 block above it
        void RecordMips(rhi::CommandBuffer& command_buffer) const
        {
            const rhi::ManifestProgram& program = *Program(Pass::Mips).Program;
            RhiAssert(command_buffer.BindShaders(program.Program()));
            uint32_t sourceLevel = 0u;

            for (uint32_t dispatch = 0u; dispatch < k_MipsDispatches && sourceLevel + 1u < MipLevels; ++dispatch)
            {
                // the resolve wrote the first source level, and the first dispatch wrote the second
                StorageToSampledBarrier(command_buffer);
                const uint32_t sourceSize = Settings.Size >> sourceLevel;
                const uint32_t levelCount = std::min(k_MipsLevelsPerDispatch, MipLevels - 1u - sourceLevel);
                const uint32_t groups = std::max(sourceSize / k_MipsSourceTile, 1u);
                PushConstants(command_buffer, program, MipsPushConstants{ sourceSize, levelCount });

                for (uint32_t image = 0u; image < k_OutputImageCount; ++image)
                {
                    const rhi::DescriptorSetHandle set{ reinterpret_cast<uint64_t>(MipSets[image][dispatch]) };
                    command_buffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, program.PipelineLayout(), 0, std::span{ &set, 1 });
                    command_buffer.Dispatch(groups, groups, k_CascadeCount);
                }

                sourceLevel += k_MipsLevelsPerDispatch;
            }
        }
    };

    OceanSimulationImpl::~OceanSimulationImpl()
    {
        if (Device == nullptr)
        {
            return;
        }

        const VkDevice vkDevice = Device->Handle().As<VkDevice>();

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(vkDevice, DescriptorPool, nullptr);
        }

        for (const std::vector<VkImageView>& views : MipViews)
        {
            for (const VkImageView view : views)
            {
                vkDestroyImageView(vkDevice, view, nullptr);
            }
        }

        std::vector<std::shared_ptr<MessageReply>> replies;
        for (const GraphicsResource& resource : Buffers)
        {
            if (resource)
            {
                replies.push_back(Resources->DestroyResource(resource));
            }
        }

        for (const GraphicsResource& resource : { CascadeParamsBuffer, SpectrumParamsBuffer, NormalizationLut, DisplacementImage, DerivativesImage })
        {
            if (resource)
            {
                replies.push_back(Resources->DestroyResource(resource));
            }
        }

        for (const std::shared_ptr<MessageReply>& reply : replies)
        {
            (void)reply->WaitForCompletion(k_TimeoutNs);
        }
    }

    uint32_t PickWaveSize(const rhi::Device& device) noexcept
    {
        const VkPhysicalDevice physicalDevice = device.GetPhysicalDevice().As<VkPhysicalDevice>();
        VkPhysicalDeviceVulkan13Properties properties13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES };
        VkPhysicalDeviceSubgroupProperties subgroupProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, &properties13 };
        VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &subgroupProperties };
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

        VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &features13 };
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

        // WaveReadLaneAt with a lane that is not uniform is a shuffle
        constexpr VkSubgroupFeatureFlags k_RequiredOperations = VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_SHUFFLE_BIT;
        const uint32_t waveSize = subgroupProperties.subgroupSize;
        const bool sizeIsAnAxisValue = waveSize == 16u || waveSize == 32u || waveSize == 64u || waveSize == 128u;
        const bool supported =
            sizeIsAnAxisValue &&
            (subgroupProperties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0u &&
            (subgroupProperties.supportedOperations & k_RequiredOperations) == k_RequiredOperations &&
            features13.subgroupSizeControl == VK_TRUE &&
            features13.computeFullSubgroups == VK_TRUE &&
            (properties13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0u &&
            properties13.minSubgroupSize <= waveSize && waveSize <= properties13.maxSubgroupSize;
        return supported ? waveSize : 0u;
    }

    CascadeParameters MakeCascadeParameters(uint32_t size, const std::array<float, k_CascadeCount>& length_scales) noexcept
    {
        // cascade i takes the wave numbers below six samples per wavelength of cascade i + 1. The lowest bound
        // excludes k = 0 (the mean height), and the last cascade has no upper bound
        constexpr float k_SamplesPerWavelength = 6.0f;
        CascadeParameters parameters{};
        parameters.LengthScales = length_scales;
        parameters.Dimensions = { size, size };
        float lowerBound = 0.0001f;

        for (uint32_t i = 0; i < k_CascadeCount; ++i)
        {
            parameters.CutoffsLow[i] = lowerBound;
            const float upperBound = (i + 1u < k_CascadeCount) ?
                2.0f * std::numbers::pi_v<float> / length_scales[i + 1u] * k_SamplesPerWavelength :
                9999.0f;
            parameters.CutoffsHigh[i] = upperBound;
            lowerBound = upperBound;
        }

        return parameters;
    }

    namespace
    {
        struct UniformLayoutCheck
        {
            std::string_view Resource;
            uint32_t ByteSize;
            std::vector<std::pair<std::string_view, uint32_t>> Members;
        };

        // the uniform structs above are written by hand, so check them against what the cook reflected
        [[nodiscard]] std::optional<std::string> CheckUniformLayouts(const lodestone::manifest::EntryPointInstanceView& instance)
        {
            const std::array<UniformLayoutCheck, 2> checks
            {
                UniformLayoutCheck{ "CascadeParams", sizeof(CascadeParameters), 
                {
                    { "LengthScales", offsetof(CascadeParameters, LengthScales) },
                    { "CutoffsHigh", offsetof(CascadeParameters, CutoffsHigh) },
                    { "CutoffsLow", offsetof(CascadeParameters, CutoffsLow) },
                    { "Dimensions", offsetof(CascadeParameters, Dimensions) }
                }
                },
                UniformLayoutCheck{ "SpectrumParams", sizeof(SpectrumParameters),
                {
                    { "NoiseSeed", offsetof(SpectrumParameters, NoiseSeed) },
                    { "DirectionalSpreadingMode", offsetof(SpectrumParameters, DirectionalSpreadingMode) },
                    { "WindSpeed", offsetof(SpectrumParameters, WindSpeed) },
                    { "Fetch", offsetof(SpectrumParameters, Fetch) },
                    { "Depth", offsetof(SpectrumParameters, Depth) },
                    { "SwellAmount", offsetof(SpectrumParameters, SwellAmount) },
                    { "Alignment", offsetof(SpectrumParameters, Alignment) },
                    { "LocalWindDirection", offsetof(SpectrumParameters, LocalWindDirection) },
                    { "SwellDirection", offsetof(SpectrumParameters, SwellDirection) },
                    { "FadeLength", offsetof(SpectrumParameters, FadeLength) },
                    { "PeakingFactor", offsetof(SpectrumParameters, PeakingFactor) },
                    { "WaveHeightScale", offsetof(SpectrumParameters, WaveHeightScale) },
                    { "SwellScale", offsetof(SpectrumParameters, SwellScale) },
                    { "SwellWindSpeed", offsetof(SpectrumParameters, SwellWindSpeed) },
                    { "SwellFetch", offsetof(SpectrumParameters, SwellFetch) },
                    { "SwellPeakingFactor", offsetof(SpectrumParameters, SwellPeakingFactor) },
                    { "SwellSpread", offsetof(SpectrumParameters, SwellSpread) },
                    { "SwellFadeLength", offsetof(SpectrumParameters, SwellFadeLength) }
                }
                },
            };

            for (const UniformLayoutCheck& check : checks)
            {
                for (const lodestone::manifest::ResolvedResource resource : instance.Layout())
                {
                    if (resource.Name() != check.Resource)
                    {
                        continue;
                    }

                    if (resource.ByteSize() < check.ByteSize)
                    {
                        return std::format("{} is {} bytes in the manifest, {} here", check.Resource, resource.ByteSize(), check.ByteSize);
                    }

                    for (const auto& [memberName, offset] : check.Members)
                    {

                        bool found = false;
                        for (const lodestone::manifest::UniformMemberView member : resource.Members())
                        {
                            // the manifest may qualify the name, so match its last component whole: a bare ends_with
                            // also took SwellWindSpeed for WindSpeed
                            const std::string_view name = member.Name();
                            const bool nameMatches = name == memberName ||
                                (name.ends_with(memberName) && name[name.size() - memberName.size() - 1u] == '.');
                            if (nameMatches)
                            {
                                found = true;
                                if (member.Offset() != offset)
                                {
                                    return std::format("{}.{} is at offset {} in the manifest, {} here", check.Resource, memberName, member.Offset(), offset);
                                }
                            }
                        }
                        
                        if (!found)
                        {
                            return std::format("{}.{} is not in the manifest", check.Resource, memberName);
                        }
                    }
                }
            }
            return std::nullopt;
        }
    }

    std::expected<std::unique_ptr<OceanSimulation>, std::string> OceanSimulation::Create(
        rhi::Device& device,
        ResourceContext& resources,
        const rhi::ShaderLibrary& library,
        const OceanSimulationSettings& settings)
    {
        auto impl = std::make_unique<OceanSimulationImpl>();
        impl->Device = &device;
        impl->Resources = &resources;
        impl->Settings = settings;
        const uint32_t size = settings.Size;
        const VkDevice vkDevice = device.Handle().As<VkDevice>();

        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(k_ModuleName);
        if (!index.has_value())
        {
            return std::unexpected(std::format("module {}: {}", k_ModuleName, rhi::ToString(index.error())));
        }

        // one variant for every pass: the wave axes are inert for all but the IFFT kernels, so the same key works
        lodestone::ManifestQueryBuilder query = (*index)->Query().Where("IFFT_SIZE", size).Where("IFFT_USE_WAVE_OPS", settings.WaveSize != 0u);
        if (settings.WaveSize != 0u)
        {
            query = query.Where("IFFT_WAVE_SIZE", settings.WaveSize);
        }

        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = query.SingleFor(k_EntryPoints);
        if (!variant.has_value())
        {
            return std::unexpected(std::format("no variant for size {} and wave size {} (query error {}, axis '{}')",
                size, settings.WaveSize, static_cast<int>(variant.error().Code), variant.error().AxisName));
        }

        const lodestone::manifest::EnvironmentView& environment = (*index)->View();
        std::optional<lodestone::manifest::VariantView> variantView = environment.VariantByKey(*variant);
        if (!variantView.has_value())
        {
            return std::unexpected(std::string{ "the selected variant is not in the environment" });
        }

        {
            lodestone::manifest::ManifestResult<uint32_t> initIndex = environment.Module().FindEntryPointIndex(k_EntryPoints[0]);
            if (!initIndex.has_value())
            {
                return std::unexpected(std::string{ "SpectrumInitCS is not in the module" });
            }
            if (std::optional<std::string> mismatch = CheckUniformLayouts(variantView->EntryPoint(*initIndex)); mismatch.has_value())
            {
                return std::unexpected(*mismatch);
            }
        }

        // buffers: every spectrum buffer has one element per (x, y, cascade). IfftInput has two slices of that
        const uint64_t elementCount = uint64_t{ size } * size * k_CascadeCount;
        const std::array<uint64_t, 4> bufferBytes{ elementCount * 4u, elementCount * 4u, elementCount * 8u, elementCount * 8u * 2u };
        constexpr std::array<const char*, 4> k_BufferNames{ "Ocean_h0K", "Ocean_h0KConjugate", "Ocean_WaveData", "Ocean_IfftInput" };
        std::array<std::shared_ptr<GraphicsResourceReply>, 4> bufferReplies;
        for (size_t i = 0; i < bufferReplies.size(); ++i)
        {
            BufferCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Flags = ResourceCreationFlags::UserDataAsNameString;
            // copies both ways, so tests can upload IFFT input and read every buffer back
            info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource | BufferUsageBits::CopyDestination;
            info.Size = bufferBytes[i];
            info.UserData = const_cast<char*>(k_BufferNames[i]);
            bufferReplies[i] = resources.CreateBuffer(info, {});
            impl->BufferBytes[i] = bufferBytes[i];
        }

        const CascadeParameters cascadeParameters = MakeCascadeParameters(size, settings.LengthScales);
        const RhiBufferResourceData cascadeData{ std::as_bytes(std::span{ &cascadeParameters, 1 }), 0u };
        BufferCreateInfo cascadeInfo{};
        cascadeInfo.Domain = ResourceDomain::CPUToGPU;
        cascadeInfo.Usage = BufferUsageBits::ComputeShaderUBO;
        cascadeInfo.Size = sizeof(CascadeParameters);
        std::shared_ptr<GraphicsResourceReply> cascadeReply = resources.CreateBuffer(cascadeInfo, std::span{ &cascadeData, 1 });

        const RhiBufferResourceData spectrumData{ std::as_bytes(std::span{ &settings.Spectrum, 1 }), 0u };
        BufferCreateInfo spectrumInfo{};
        spectrumInfo.Domain = ResourceDomain::CPUToGPU;
        spectrumInfo.Usage = BufferUsageBits::ComputeShaderUBO;
        // std140 rounds a uniform block up to 16 bytes
        spectrumInfo.Size = (sizeof(SpectrumParameters) + 15u) & ~size_t{ 15u };
        std::shared_ptr<GraphicsResourceReply> spectrumReply = resources.CreateBuffer(spectrumInfo, std::span{ &spectrumData, 1 });

        // the Donelan-Banner normalization LUT, generated by assets/scripts/ocean_fft_support.py at build time: f32 bits,
        // x = beta, y = square root mapped swell (SampleDonelanBannerLUT in the generated Slang reads it)
        RhiImageResourceData lutData{};
        lutData.Data = std::as_bytes(std::span{ DonelanBannerNormLUT });
        lutData.Width = DonelanBannerNormLUTWidth;
        lutData.Height = DonelanBannerNormLUTHeight;

        ImageCreateInfo lutInfo{};
        lutInfo.Domain = ResourceDomain::GPUOnly;
        lutInfo.Usage = ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader;
        lutInfo.Type = ImageType::Type2D;
        lutInfo.Format = CommonFormats::R32_Float;
        lutInfo.Width = DonelanBannerNormLUTWidth;
        lutInfo.Height = DonelanBannerNormLUTHeight;
        lutInfo.Depth = 1u;
        lutInfo.ViewInfo = ImageViewCreateInfo{};
        SamplerCreateInfo lutSampler{};
        lutSampler.AddressU = SamplerCreateInfo::AddressMode::ClampToEdge;
        lutSampler.AddressV = SamplerCreateInfo::AddressMode::ClampToEdge;
        lutSampler.AddressW = SamplerCreateInfo::AddressMode::ClampToEdge;
        lutInfo.SamplerInfo = lutSampler;
        std::shared_ptr<GraphicsResourceReply> lutReply = resources.CreateImage(lutInfo, std::span{ &lutData, 1 });

        // the outputs are storage images for the resolve and sampled images for the renderer, and stay in General.
        // A full mip chain, down to 1x1: the renderer samples distant water from the smaller mips. One view covers
        // every mip, and a storage write through it goes to its base level, mip 0. Copies out are for tests
        impl->MipLevels = static_cast<uint32_t>(std::bit_width(size));
        std::array<std::shared_ptr<GraphicsResourceReply>, k_OutputImageCount> imageReplies;

        if (impl->MipLevels - 1u > k_MipsLevelsPerDispatch * k_MipsDispatches)
        {
            return std::unexpected(std::format("size {} has more mip levels than OceanMipsCS writes in {} dispatches", size, k_MipsDispatches));
        }

        for (std::shared_ptr<GraphicsResourceReply>& reply : imageReplies)
        {
            ImageCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Usage = ImageUsageBits::UavComputeShader | ImageUsageBits::SampledImage | ImageUsageBits::SrvVertexShader |
                ImageUsageBits::SrvFragmentShader | ImageUsageBits::SrvComputeShader | ImageUsageBits::TransferSource;
            info.Type = ImageType::Type2D;
            info.Format = CommonFormats::RGBA16_Float;
            info.Width = size;
            info.Height = size;
            info.Depth = 1u;
            info.MipLevels = impl->MipLevels;
            info.ArrayLayers = k_CascadeCount;
            info.ViewInfo = ImageViewCreateInfo{};
            info.ViewInfo->ViewType = ImageViewType::Type2DArray;
            info.ViewInfo->Range.LevelCount = impl->MipLevels;
            info.ViewInfo->Range.LayerCount = k_CascadeCount;
            reply = resources.CreateImage(info, {});
        }

        for (size_t i = 0; i < bufferReplies.size(); ++i)
        {
            if (!WaitForReply(bufferReplies[i]))
            {
                return std::unexpected(std::format("creating {} failed", k_BufferNames[i]));
            }
            impl->Buffers[i] = bufferReplies[i]->GetResource();
        }

        if (!WaitForReply(cascadeReply) || !WaitForReply(spectrumReply) || !WaitForReply(lutReply) ||
            !WaitForReply(imageReplies[0]) || !WaitForReply(imageReplies[1]))
        {
            return std::unexpected(std::string{ "creating the uniform buffers or the images failed" });
        }

        impl->CascadeParamsBuffer = cascadeReply->GetResource();
        impl->SpectrumParamsBuffer = spectrumReply->GetResource();
        impl->NormalizationLut = lutReply->GetResource();
        impl->DisplacementImage = imageReplies[0]->GetResource();
        impl->DerivativesImage = imageReplies[1]->GetResource();
        const std::array<GraphicsResource, k_OutputImageCount> outputImages{ impl->DisplacementImage, impl->DerivativesImage };

        for (uint32_t image = 0u; image < k_OutputImageCount; ++image)
        {
            std::vector<VkImageView>& views = impl->MipViews[image];

            for (uint32_t level = 0u; level < impl->MipLevels; ++level)
            {
                const VkImageViewCreateInfo viewInfo
                {
                    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0,
                    reinterpret_cast<VkImage>(outputImages[image].VkHandle), VK_IMAGE_VIEW_TYPE_2D_ARRAY, VK_FORMAT_R16G16B16A16_SFLOAT,
                    VkComponentMapping{}, VkImageSubresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, level, 1u, 0u, k_CascadeCount }
                };
                VkImageView view{ VK_NULL_HANDLE };

                if (vkCreateImageView(vkDevice, &viewInfo, nullptr, &view) != VK_SUCCESS)
                {
                    return std::unexpected(std::format("creating the view of mip {} failed", level));
                }

                views.push_back(view);
            }
        }

        // every pass has one descriptor set, set 0, with only the resources it reads. OceanMipsCS has one per image
        // and dispatch, with the levels of that dispatch
        constexpr uint32_t k_MipSetCount = k_OutputImageCount * k_MipsDispatches;
        constexpr std::array<VkDescriptorPoolSize, 5> k_PoolSizes
        {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8 + k_MipSetCount },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, 8 },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8 + k_MipSetCount * k_MipsLevelsPerDispatch },
        };
        const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0,
            static_cast<uint32_t>(impl->Programs.size()) + k_MipSetCount, static_cast<uint32_t>(k_PoolSizes.size()), k_PoolSizes.data() };

        if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &impl->DescriptorPool) != VK_SUCCESS)
        {
            return std::unexpected(std::string{ "descriptor pool creation failed" });
        }

        const BoundResources resourcesByName
        {
            { "h0K", { impl->Buffers[0] } },
            { "h0KConjugate", { impl->Buffers[1] } },
            { "WaveData", { impl->Buffers[2] } },
            { "IfftInput", { impl->Buffers[3] } },
            { "CascadeParams", { impl->CascadeParamsBuffer } },
            { "SpectrumParams", { impl->SpectrumParamsBuffer } },
            { "normTexture", { impl->NormalizationLut } },
            { "normSampler", { impl->NormalizationLut } },
            { "Displacement", { impl->DisplacementImage } },
            { "Derivatives", { impl->DerivativesImage } },
        };

        for (size_t passIndex = 0; passIndex < impl->Programs.size(); ++passIndex)
        {
            const std::string_view entryPoint = k_EntryPoints[passIndex];
            const bool isIfft = passIndex == static_cast<size_t>(Pass::IfftRadix2) || passIndex == static_cast<size_t>(Pass::IfftRadix4);
            rhi::ManifestProgramOptions options{};

            if (isIfft && settings.WaveSize != 0u)
            {
                // the wave-op stages index lanes with the local thread index, so each subgroup must be full and exactly WaveSize
                options.RequiredSubgroupSize = settings.WaveSize;
                options.RequireFullSubgroups = true;
            }

            const std::array<std::string_view, 1> entryPoints{ entryPoint };
            rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program =
                library.CreateProgram(device.Handle(), k_ModuleName, *variant, entryPoints, options);

            if (!program.has_value())
            {
                return std::unexpected(std::format("{}: {}", entryPoint, rhi::ToString(program.error())));
            }

            PassProgram& passProgram = impl->Programs[passIndex];
            passProgram.Program = std::move(*program);

            // the mip sets are written below, with the views of single levels
            if (passIndex == static_cast<size_t>(Pass::Mips))
            {
                continue;
            }

            const VkDescriptorSetLayout setLayout = passProgram.Program->SetLayouts()[0].As<VkDescriptorSetLayout>();
            const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl->DescriptorPool, 1, &setLayout };

            if (vkAllocateDescriptorSets(vkDevice, &setInfo, &passProgram.Set) != VK_SUCCESS)
            {
                return std::unexpected(std::format("{}: descriptor set allocation failed", entryPoint));
            }

            if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, passProgram.Set, **index, *variant, entryPoints, resourcesByName); error.has_value())
            {
                return std::unexpected(*error);
            }
        }

        const rhi::ManifestProgram& mipsProgram = *impl->Program(Pass::Mips).Program;
        const VkDescriptorSetLayout mipsSetLayout = mipsProgram.SetLayouts()[0].As<VkDescriptorSetLayout>();
        const std::array<std::string_view, 1> mipsEntryPoints{ k_EntryPoints[static_cast<size_t>(Pass::Mips)] };

        for (uint32_t image = 0u; image < k_OutputImageCount; ++image)
        {
            const std::vector<VkImageView>& views = impl->MipViews[image];

            for (uint32_t dispatch = 0u; dispatch < k_MipsDispatches; ++dispatch)
            {
                // a view of one level, as the descriptor writer reads it: the image handle and that view
                const auto levelResource = [&](uint32_t level)
                {
                    GraphicsResource resource = outputImages[image];
                    resource.VkViewHandle = reinterpret_cast<uint64_t>(views[std::min(level, impl->MipLevels - 1u)]);
                    return resource;
                };

                const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, impl->DescriptorPool, 1, &mipsSetLayout };
                VkDescriptorSet& set = impl->MipSets[image][dispatch];

                if (vkAllocateDescriptorSets(vkDevice, &setInfo, &set) != VK_SUCCESS)
                {
                    return std::unexpected(std::string{ "OceanMipsCS: descriptor set allocation failed" });
                }

                // a dispatch with fewer than five levels to write still binds every target. The extra ones view the
                // last level, and the shader never writes them
                const uint32_t sourceLevel = dispatch * k_MipsLevelsPerDispatch;
                BoundResources mipResources{ { "MipSource", { levelResource(sourceLevel), VK_IMAGE_LAYOUT_GENERAL } } };

                for (uint32_t target = 0u; target < k_MipsLevelsPerDispatch; ++target)
                {
                    mipResources.emplace(k_MipTargetNames[target], BoundResource{ levelResource(sourceLevel + 1u + target), VK_IMAGE_LAYOUT_GENERAL });
                }

                if (std::optional<std::string> error = WriteManifestDescriptors(vkDevice, set, **index, *variant, mipsEntryPoints, mipResources); error.has_value())
                {
                    return std::unexpected(*error);
                }
            }
        }

        return std::make_unique<OceanSimulation>(std::move(impl));
    }

    OceanSimulation::OceanSimulation(std::unique_ptr<OceanSimulationImpl>&& _impl) noexcept : impl{ std::move(_impl) } {}
    OceanSimulation::~OceanSimulation() = default;

    void OceanSimulation::RecordInitialize(rhi::CommandBuffer& command_buffer) const
    {
        // the tick writes mip 0 with the resolve and the other mips with OceanMipsCS, so both wait for the transition
        const std::array<rhi::ImageBarrier, 2> toGeneral
        {
            rhi::ImageBarrier{ rhi::ImageHandle{ impl->DisplacementImage.VkHandle },
                rhi::PipelineStage::None, rhi::Access::None,
                k_TickStages, k_TickWriteAccess,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::General },
            rhi::ImageBarrier{ rhi::ImageHandle{ impl->DerivativesImage.VkHandle },
                rhi::PipelineStage::None, rhi::Access::None,
                k_TickStages, k_TickWriteAccess,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::General },
        };
        command_buffer.Barrier(rhi::DependencyInfo{ {}, {}, toGeneral });

        const uint32_t groups = impl->SpectrumGroups();
        impl->Bind(command_buffer, Pass::SpectrumInit);
        command_buffer.Dispatch(groups, groups, 1);
        ComputeBarrier(command_buffer);
        impl->Bind(command_buffer, Pass::SpectrumConjugate);
        command_buffer.Dispatch(groups, groups, 1);
        ComputeBarrier(command_buffer);
    }

    void OceanSimulation::RecordIfft(rhi::CommandBuffer& command_buffer) const
    {
        const Pass pass = impl->Settings.Kernel == IfftKernel::Radix2 ? Pass::IfftRadix2 : Pass::IfftRadix4;
        const rhi::ManifestProgram& program = *impl->Program(pass).Program;
        impl->Bind(command_buffer, pass);
        // one workgroup per row of every slice of every cascade
        const uint32_t slices = k_CascadeCount * 2u;
        for (const uint32_t transpose : { 0u, 1u })
        {
            PushConstants(command_buffer, program, IfftPushConstants{ transpose, impl->Settings.WaveSize, 0u, 0u });
            command_buffer.Dispatch(1, impl->Settings.Size, slices);
            ComputeBarrier(command_buffer);
        }
    }

    void OceanSimulation::RecordTick(rhi::CommandBuffer& command_buffer, rhi::GpuTimerBlock& timers, float time_seconds) const
    {
        const uint32_t groups = impl->SpectrumGroups();

        // a scope's timestamps are written when all earlier work is done, so each pass's time is its own
        {
            const rhi::ScopedGpuTimer timer(timers, "Spectrum tick");
            impl->Bind(command_buffer, Pass::Tick);
            PushConstants(command_buffer, *impl->Program(Pass::Tick).Program, TickPushConstants{ time_seconds });
            command_buffer.Dispatch(groups, groups, 1);
            ComputeBarrier(command_buffer);
        }

        {
            const rhi::ScopedGpuTimer timer(timers, "IFFT");
            RecordIfft(command_buffer);
        }

        {
            const rhi::ScopedGpuTimer timer(timers, "Resolve");
            impl->Bind(command_buffer, Pass::Resolve);
            PushConstants(command_buffer, *impl->Program(Pass::Resolve).Program, ResolvePushConstants{ impl->Settings.Choppiness });
            command_buffer.Dispatch(groups, groups, k_CascadeCount);
        }

        {
            const rhi::ScopedGpuTimer timer(timers, "Mips");
            impl->RecordMips(command_buffer);
        }
    }

    bool OceanSimulation::SetSpectrum(const SpectrumParameters& spectrum)
    {
        impl->Settings.Spectrum = spectrum;
        const RhiBufferResourceData data{ std::as_bytes(std::span{ &spectrum, 1 }), 0u };
        std::shared_ptr<ResourceTransferReply> reply = impl->Resources->SetBufferData(impl->SpectrumParamsBuffer, std::span{ &data, 1 });
        return reply != nullptr && reply->WaitForCompletion(k_TimeoutNs) == MessageReply::Status::Completed;
    }

    void OceanSimulation::SetChoppiness(float choppiness) noexcept
    {
        impl->Settings.Choppiness = choppiness;
    }

    const OceanSimulationSettings& OceanSimulation::Settings() const noexcept
    {
        return impl->Settings;
    }

    GraphicsResource OceanSimulation::GetBuffer(Buffer buffer) const noexcept
    {
        return impl->Buffers[static_cast<size_t>(buffer)];
    }

    uint64_t OceanSimulation::BufferBytes(Buffer buffer) const noexcept
    {
        return impl->BufferBytes[static_cast<size_t>(buffer)];
    }

    GraphicsResource OceanSimulation::Displacement() const noexcept
    {
        return impl->DisplacementImage;
    }

    GraphicsResource OceanSimulation::Derivatives() const noexcept
    {
        return impl->DerivativesImage;
    }

    uint32_t OceanSimulation::MipLevels() const noexcept
    {
        return impl->MipLevels;
    }

    float HalfToFloat(uint16_t value) noexcept
    {
        const uint32_t sign = (value & 0x8000u) << 16u;
        const uint32_t exponent = (value >> 10u) & 0x1Fu;
        const uint32_t mantissa = value & 0x3FFu;

        if (exponent == 0u)
        {
            // zero or denormal: mantissa * 2^-24
            const float magnitude = std::ldexp(static_cast<float>(mantissa), -24);
            return sign != 0u ? -magnitude : magnitude;
        }

        if (exponent == 31u)
        {
            return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13u));
        }

        return std::bit_cast<float>(sign | ((exponent + 112u) << 23u) | (mantissa << 13u));
    }

    uint16_t FloatToHalf(float value) noexcept
    {
        const uint32_t bits = std::bit_cast<uint32_t>(value);
        const uint16_t sign = static_cast<uint16_t>((bits >> 16u) & 0x8000u);
        const float magnitude = std::fabs(value);

        if (std::isnan(value))
        {
            return static_cast<uint16_t>(sign | 0x7E00u);
        }

        if (magnitude >= 65520.0f)
        {
            return static_cast<uint16_t>(sign | 0x7C00u);
        }

        if (magnitude < 6.103515625e-05f)
        {
            // denormal: round to the nearest multiple of 2^-24
            return static_cast<uint16_t>(sign | static_cast<uint16_t>(std::nearbyint(magnitude * 16777216.0f)));
        }
        
        // round to nearest even on the 13 dropped mantissa bits
        const uint32_t magnitudeBits = bits & 0x7FFFFFFFu;
        const uint32_t rounded = magnitudeBits + 0x0FFFu + ((magnitudeBits >> 13u) & 1u);
        return static_cast<uint16_t>(sign | ((rounded - (112u << 23u)) >> 13u));
    }
}
