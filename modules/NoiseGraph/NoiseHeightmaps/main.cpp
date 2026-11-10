#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiAssert.hpp"
#include "RhiFlags.hpp"
#include "RhiHandle.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifestLayout.hpp"
#include "ShaderProgram.hpp"
#include "VariantKey.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// stb's HDR writer calls sprintf, and MSVC warns about it (C4996). This app writes PNG only
#pragma warning(push)
#pragma warning(disable : 4996)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma warning(pop)

// Renders sample heightmaps of the noise library headless: NoiseFractalCS for one basis and rule at a time, read back as
// value and gradient, written as a grayscale height PNG and a shaded-relief PNG. Each sample also renders a small patch
// at a fine spacing and compares the autodiff gradient with central differences of the value.
//
// Usage: NoiseHeightmaps <output folder>

namespace
{
    constexpr int32_t k_RenderSize = 16384;
    constexpr int32_t k_ImageSize = 8192;
    constexpr int32_t k_Downsample = k_RenderSize / k_ImageSize;
    constexpr float k_RenderExtent = 5.0f;

    // fine enough that the finest octave spans about 39 elements, so central differences are accurate
    constexpr int32_t k_CheckSize = 256;
    constexpr float k_CheckSpacing = 0.0002f;
    constexpr std::array<float, 2> k_CheckOrigin{ 1.37f, 2.71f };

    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;
    constexpr std::string_view k_ModuleName{ "NoiseKernels" };
    constexpr std::array<std::string_view, 1> k_EntryPoints{ "NoiseFractalCS" };
    constexpr int32_t k_MaxRuleParameters = 7;

    using Element = std::array<float, 4>;

    struct RuleParameter
    {
        std::string_view Name;
        float Value;
    };

    struct HeightmapSample
    {
        std::string_view Name;
        std::string_view Basis;
        std::string_view Rule;
        float Frequency;
        float Lacunarity;
        int32_t OctaveCount;
        uint32_t Seed;
        int32_t RotateOctaves;
        std::array<RuleParameter, k_MaxRuleParameters> RuleParameters;
        int32_t RuleParameterCount;
    };

    struct GridArea
    {
        std::array<float, 2> Origin;
        float Spacing;
        int32_t Size;
    };

    struct GradientCheck
    {
        double RelativeRmsError;
        double MedianRelativeError;
    };

    // de Carpentier's defaults where the rule has them; 8 octaves keep the finest octave above 2 texels at 2048
    const std::array<HeightmapSample, 5> k_Samples
    {
        HeightmapSample{ "fbm_perlin", "Perlin3", "Fbm", 1.0f, 2.0f, 8, 7u, 1, { RuleParameter{ "gain", 0.5f } }, 1 },
        HeightmapSample{ "ridgedmulti_simplex", "Simplex3", "RidgedMulti", 1.0f, 2.0f, 8, 7u, 1,
                         { RuleParameter{ "offset", 1.0f }, RuleParameter{ "gain", 2.0f }, RuleParameter{ "spectralExponent", 1.0f } }, 3 },
        HeightmapSample{ "swiss_perlin", "Perlin3", "Swiss", 1.0f, 2.0f, 8, 7u, 1,
                         { RuleParameter{ "gain", 0.5f }, RuleParameter{ "warpStrength", 0.15f }, RuleParameter{ "compoundingWarp", 0.0f } }, 3 },
        HeightmapSample{ "swiss_perlin_compounding", "Perlin3", "Swiss", 1.0f, 2.0f, 8, 7u, 1,
                         { RuleParameter{ "gain", 0.5f }, RuleParameter{ "warpStrength", 0.02f }, RuleParameter{ "compoundingWarp", 1.0f } }, 3 },
        HeightmapSample{ "jordan_perlin", "Perlin3", "Jordan", 1.0f, 2.0f, 8, 7u, 1,
                         { RuleParameter{ "gain1", 0.8f }, RuleParameter{ "gain", 0.5f }, RuleParameter{ "warp0", 0.4f },
                           RuleParameter{ "warp", 0.35f }, RuleParameter{ "damp0", 1.0f }, RuleParameter{ "damp", 0.8f },
                           RuleParameter{ "dampScale", 1.0f } }, 7 },
    };

    [[nodiscard]] std::optional<std::vector<Element>> RenderArea(rhi::RhiSystem& rhi_system,
                                                                 ResourceContext& resources,
                                                                 const rhi::ShaderLibrary& library,
                                                                 const HeightmapSample& sample,
                                                                 const GridArea& area);
    [[nodiscard]] bool WriteParameters(std::span<std::byte> bytes,
                                       lodestone::manifest::UniformMemberRange members,
                                       const HeightmapSample& sample,
                                       const GridArea& area);
    [[nodiscard]] bool WriteMember(std::span<std::byte> bytes,
                                   lodestone::manifest::UniformMemberRange members,
                                   std::string_view path,
                                   std::span<const std::byte> value);
    [[nodiscard]] std::vector<Element> DownsampleElements(std::span<const Element> elements);
    [[nodiscard]] bool WriteHeightPng(const std::filesystem::path& path, std::span<const Element> elements);
    [[nodiscard]] bool WriteShadedPng(const std::filesystem::path& path, std::span<const Element> elements);
    [[nodiscard]] std::array<float, 3> TerrainColor(float height);
    [[nodiscard]] GradientCheck CheckGradient(std::span<const Element> elements, float spacing);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: NoiseHeightmaps <output folder>\n";
        return 1;
    }

    const std::filesystem::path outputFolder{ argv[1] };
    std::filesystem::create_directories(outputFolder);

    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "NoiseHeightmaps";
    createInfo.VkVersion = rhi::ApiVersion::Latest;
    createInfo.ValidationLevel = rhi::ValidationLayers::Full;
    createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
    createInfo.RequiredDeviceExtensions = { "VK_EXT_shader_object" };
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(NOISE_HEIGHTMAPS_MANIFEST);

    if (!library.has_value())
    {
        std::cerr << std::format("Failed to open {}: {}\n", NOISE_HEIGHTMAPS_MANIFEST, rhi::ToString(library.error()));
        return 1;
    }

    int32_t failures = 0;

    {
        ResourceContext resources;
        const ResourceContextError resourcesResult = resources.Initialize(ResourceContextCreateInfo{ rhiSystem->GetInstance(), rhiSystem->GetDevice(), rhiSystem->GetVulkanApiVersion(), true });

        if (resourcesResult != ResourceContextError::Success)
        {
            std::cerr << std::format("NoiseHeightmaps: ResourceContext::Initialize failed: {}\n", ToString(resourcesResult));
            return 1;
        }

        const float renderSpacing = k_RenderExtent / static_cast<float>(k_RenderSize);
        const GridArea renderArea{ { 0.0f, 0.0f }, renderSpacing, k_RenderSize };
        const GridArea checkArea{ k_CheckOrigin, k_CheckSpacing, k_CheckSize };

        for (const HeightmapSample& sample : k_Samples)
        {
            std::optional<std::vector<Element>> rendered = RenderArea(*rhiSystem, resources, **library, sample, renderArea);
            std::optional<std::vector<Element>> checked = RenderArea(*rhiSystem, resources, **library, sample, checkArea);

            if (!rendered.has_value() || !checked.has_value())
            {
                ++failures;
                continue;
            }

            const std::vector<Element> image = DownsampleElements(*rendered);
            const bool wroteHeight = WriteHeightPng(outputFolder / std::format("{}_height.png", sample.Name), image);
            const bool wroteShaded = WriteShadedPng(outputFolder / std::format("{}_shaded.png", sample.Name), image);

            if (!wroteHeight || !wroteShaded)
            {
                std::cerr << std::format("[{}] a PNG could not be written\n", sample.Name);
                ++failures;
            }

            const auto [lowest, highest] = std::ranges::minmax(*rendered | std::views::transform([](const Element& element) { return element[0]; }));
            const GradientCheck check = CheckGradient(*checked, k_CheckSpacing);
            std::cout << std::format("[{}] height range [{:.4f}, {:.4f}]; autodiff gradient vs central differences: relative RMS {:.4f}, median {:.4f}\n",
                                     sample.Name, lowest, highest, check.RelativeRmsError, check.MedianRelativeError);
        }
    }

    if (failures != 0)
    {
        std::cerr << std::format("NoiseHeightmaps: {} failure(s)\n", failures);
        return 1;
    }

    std::cout << std::format("NoiseHeightmaps: wrote {} samples to {}\n", k_Samples.size(), outputFolder.string());
    return 0;
}

namespace
{
    std::optional<std::vector<Element>> RenderArea(rhi::RhiSystem& rhi_system,
                                                   ResourceContext& resources,
                                                   const rhi::ShaderLibrary& library,
                                                   const HeightmapSample& sample,
                                                   const GridArea& area)
    {
        rhi::Device& device = *rhi_system.GetDevice();
        const VkDevice vkDevice = device.Handle().As<VkDevice>();
        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(k_ModuleName);

        if (!index.has_value())
        {
            std::cerr << std::format("[{}] module {} not found: {}\n", sample.Name, k_ModuleName, rhi::ToString(index.error()));
            return std::nullopt;
        }

        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = (*index)->Query()
            .Where("BASIS", lodestone::AxisValueDomain::Type, sample.Basis)
            .Where("RULE", lodestone::AxisValueDomain::Type, sample.Rule)
            .SingleFor(k_EntryPoints);

        if (!variant.has_value())
        {
            std::cerr << std::format("[{}] variant query failed, code {}, axis '{}'\n",
                                     sample.Name, static_cast<int32_t>(variant.error().Code), variant.error().AxisName);
            return std::nullopt;
        }

        const lodestone::manifest::EnvironmentView& environment = (*index)->View();
        lodestone::manifest::ManifestResult<uint32_t> entryPointIndex = environment.Module().FindEntryPointIndex(k_EntryPoints[0]);
        std::optional<lodestone::manifest::VariantView> variantView = environment.VariantByKey(*variant);

        if (!entryPointIndex.has_value() || !variantView.has_value())
        {
            std::cerr << std::format("[{}] the manifest has no {} for this variant\n", sample.Name, k_EntryPoints[0]);
            return std::nullopt;
        }

        // the node parameters and the output, by name; group and binding come from the manifest
        std::optional<lodestone::manifest::ResolvedResource> parameters;
        std::optional<lodestone::manifest::ResolvedResource> output;
        const lodestone::manifest::EntryPointInstanceView instance = variantView->EntryPoint(*entryPointIndex);

        for (const lodestone::manifest::ResolvedResource resource : instance.Layout())
        {
            if (resource.Name() == "gFractalNode")
            {
                parameters = resource;
            }

            if (resource.Name() == "gNodeOutput")
            {
                output = resource;
            }
        }

        if (!parameters.has_value() || !output.has_value())
        {
            std::cerr << std::format("[{}] the variant does not bind gFractalNode and gNodeOutput\n", sample.Name);
            return std::nullopt;
        }

        std::vector<std::byte> parameterBytes(static_cast<size_t>(parameters->ByteSize()));

        if (!WriteParameters(parameterBytes, parameters->Members(), sample, area))
        {
            return std::nullopt;
        }

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program =
            library.CreateProgram(device.Handle(), k_ModuleName, *variant, k_EntryPoints);

        if (!program.has_value())
        {
            std::cerr << std::format("[{}] program creation failed: {}\n", sample.Name, rhi::ToString(program.error()));
            return std::nullopt;
        }

        const RhiBufferResourceData parameterData{ std::span<const std::byte>{ parameterBytes }, 0u };
        BufferCreateInfo parameterInfo{};
        parameterInfo.Domain = ResourceDomain::CPUToGPU;
        parameterInfo.Usage = BufferUsageBits::ComputeShaderUBO;
        parameterInfo.Size = parameterBytes.size();
        rhi::ResourceFuture<GraphicsResource> parameterReply = resources.CreateBuffer(parameterInfo, std::span{ &parameterData, 1u });

        const size_t elementCount = static_cast<size_t>(area.Size) * static_cast<size_t>(area.Size);
        const size_t outputBytes = elementCount * sizeof(Element);
        BufferCreateInfo outputInfo{};
        outputInfo.Domain = ResourceDomain::GPUOnly;
        outputInfo.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource;
        outputInfo.Size = outputBytes;
        rhi::ResourceFuture<GraphicsResource> outputReply = resources.CreateBuffer(outputInfo, {});

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::CopyDestination;
        readbackInfo.Size = outputBytes;
        rhi::ResourceFuture<GraphicsResource> readbackReply = resources.CreateBuffer(readbackInfo, {});

        if (resources.Wait(parameterReply, k_TimeoutNs) != rhi::FutureState::Complete ||
            resources.Wait(outputReply, k_TimeoutNs) != rhi::FutureState::Complete ||
            resources.Wait(readbackReply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::cerr << std::format("[{}] ResourceContext did not create the buffers\n", sample.Name);
            return std::nullopt;
        }

        const GraphicsResource parameterBuffer = parameterReply.Get();
        const GraphicsResource outputBuffer = outputReply.Get();
        const GraphicsResource readbackBuffer = readbackReply.Get();

        constexpr VkDescriptorPoolSize k_PoolSizes[2]
        {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 },
        };
        const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 2, 2, k_PoolSizes };
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        RhiAssert(rhi::Result(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool)));

        // the parameter block and the output sit in different descriptor sets, one each
        std::span<const rhi::DescriptorSetLayoutHandle> setLayouts = (*program)->SetLayouts();
        const uint32_t parameterSet = parameters->PlacementValue().Word0;
        const uint32_t outputSet = output->PlacementValue().Word0;
        const std::array<VkDescriptorSetLayout, 2> allocatedLayouts
        {
            setLayouts[parameterSet].As<VkDescriptorSetLayout>(),
            setLayouts[outputSet].As<VkDescriptorSetLayout>(),
        };
        const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 2, allocatedLayouts.data() };
        std::array<VkDescriptorSet, 2> descriptorSets{};
        RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &setInfo, descriptorSets.data())));

        const VkDescriptorBufferInfo parameterDescriptor{ reinterpret_cast<VkBuffer>(parameterBuffer.VkHandle), 0, VK_WHOLE_SIZE };
        const VkDescriptorBufferInfo outputDescriptor{ reinterpret_cast<VkBuffer>(outputBuffer.VkHandle), 0, VK_WHOLE_SIZE };
        const VkWriteDescriptorSet writes[2]
        {
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSets[0], parameters->PlacementValue().Word1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &parameterDescriptor, nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSets[1], output->PlacementValue().Word1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputDescriptor, nullptr },
        };
        vkUpdateDescriptorSets(vkDevice, 2, writes, 0, nullptr);

        bool dispatched = false;

        {
            rhi::CommandPool commandPool(device.Handle(), rhi::CommandPool::Type::Compute, device.GetQueueFamilyIndices().Graphics);
            RhiAssert(commandPool.AllocateCommandBuffers(1));
            rhi::CommandBuffer commandBuffer(commandPool.GetCommandBuffer(0));
            RhiAssert(commandBuffer.Begin());
            RhiAssert(commandBuffer.BindShaders((*program)->Program()));

            const rhi::DescriptorSetHandle parameterSetHandle{ reinterpret_cast<uint64_t>(descriptorSets[0]) };
            const rhi::DescriptorSetHandle outputSetHandle{ reinterpret_cast<uint64_t>(descriptorSets[1]) };
            commandBuffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, (*program)->PipelineLayout(), parameterSet, std::span{ &parameterSetHandle, 1 });
            commandBuffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, (*program)->PipelineLayout(), outputSet, std::span{ &outputSetHandle, 1 });

            const std::array<uint32_t, 3> workgroup = (*program)->Workgroup();
            const uint32_t size = static_cast<uint32_t>(area.Size);
            commandBuffer.Dispatch((size + workgroup[0] - 1u) / workgroup[0], (size + workgroup[1] - 1u) / workgroup[1], 1);

            const rhi::BufferHandle outputHandle{ outputBuffer.VkHandle };
            const rhi::BufferBarrier toCopy
            {
                outputHandle,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::PipelineStage::Copy, rhi::Access::TransferRead
            };
            commandBuffer.Barrier(rhi::DependencyInfo{ {}, std::span{ &toCopy, 1 }, {} });
            const rhi::BufferCopy copyRegion{ 0u, 0u, outputBytes };
            commandBuffer.CopyBuffer(outputHandle, rhi::BufferHandle{ readbackBuffer.VkHandle }, std::span{ &copyRegion, 1 });
            const rhi::GlobalBarrier toHost{ rhi::PipelineStage::Copy, rhi::Access::TransferWrite, rhi::PipelineStage::Host, rhi::Access::HostRead };
            commandBuffer.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1 }, {}, {} });
            RhiAssert(commandBuffer.End());

            const rhi::CommandBufferHandle commandBufferHandle = commandBuffer.Handle();
            rhi::Fence fence(device.Handle());
            RhiAssert(rhi::QueueSubmit(device.GetGeneralQueue(), rhi::SubmitInfo{ {}, std::span{ &commandBufferHandle, 1 }, {} }, fence.Handle()));
            dispatched = fence.Wait();
        }

        std::optional<std::vector<Element>> result;

        if (dispatched)
        {
            rhi::ResourceFuture<void*> mapReply = resources.MapBuffer(readbackBuffer, outputBytes, 0u);

            if (resources.Wait(mapReply, k_TimeoutNs) == rhi::FutureState::Complete)
            {
                const std::span<const Element> mapped{ static_cast<const Element*>(mapReply.Get()), elementCount };
                result.emplace(mapped.begin(), mapped.end());
                rhi::OperationFuture unmapReply = resources.UnmapBuffer(readbackBuffer, outputBytes, 0u);
                (void)resources.Wait(unmapReply, k_TimeoutNs);
            }
        }

        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);

        for (const GraphicsResource& resource : { parameterBuffer, outputBuffer, readbackBuffer })
        {
            rhi::OperationFuture destroyReply = resources.DestroyResource(resource);
            (void)resources.Wait(destroyReply, k_TimeoutNs);
        }

        if (!result.has_value())
        {
            std::cerr << std::format("[{}] the dispatch or the readback failed\n", sample.Name);
        }

        return result;
    }

    bool WriteParameters(std::span<std::byte> bytes,
                         lodestone::manifest::UniformMemberRange members,
                         const HeightmapSample& sample,
                         const GridArea& area)
    {
        const std::array<float, 2> spacing{ area.Spacing, area.Spacing };
        const std::array<int32_t, 2> extent{ area.Size, area.Size };

        bool wrote = WriteMember(bytes, members, "grid.origin", std::as_bytes(std::span{ area.Origin }));
        wrote = wrote && WriteMember(bytes, members, "grid.spacing", std::as_bytes(std::span{ spacing }));
        wrote = wrote && WriteMember(bytes, members, "grid.extent", std::as_bytes(std::span{ extent }));
        wrote = wrote && WriteMember(bytes, members, "fractal.frequency", std::as_bytes(std::span{ &sample.Frequency, 1 }));
        wrote = wrote && WriteMember(bytes, members, "fractal.lacunarity", std::as_bytes(std::span{ &sample.Lacunarity, 1 }));
        wrote = wrote && WriteMember(bytes, members, "fractal.octaveCount", std::as_bytes(std::span{ &sample.OctaveCount, 1 }));
        wrote = wrote && WriteMember(bytes, members, "fractal.seed", std::as_bytes(std::span{ &sample.Seed, 1 }));
        wrote = wrote && WriteMember(bytes, members, "fractal.rotateOctaves", std::as_bytes(std::span{ &sample.RotateOctaves, 1 }));

        for (int32_t index = 0; index < sample.RuleParameterCount; ++index)
        {
            const RuleParameter& parameter = sample.RuleParameters[static_cast<size_t>(index)];
            const std::string path = std::format("fractal.rule.{}", parameter.Name);

            // the only integer rule parameter is Swiss's compoundingWarp
            if (parameter.Name == "compoundingWarp")
            {
                const int32_t flag = static_cast<int32_t>(parameter.Value);
                wrote = wrote && WriteMember(bytes, members, path, std::as_bytes(std::span{ &flag, 1 }));
                continue;
            }

            wrote = wrote && WriteMember(bytes, members, path, std::as_bytes(std::span{ &parameter.Value, 1 }));
        }

        if (!wrote)
        {
            std::cerr << std::format("[{}] a parameter path is missing from gFractalNode\n", sample.Name);
        }

        return wrote;
    }

    bool WriteMember(std::span<std::byte> bytes,
                     lodestone::manifest::UniformMemberRange members,
                     std::string_view path,
                     std::span<const std::byte> value)
    {
        std::optional<lodestone::manifest::UniformMemberView> member = members.Find(path);

        if (!member.has_value() || static_cast<size_t>(member->Size()) != value.size())
        {
            std::cerr << std::format("member '{}' not found, or its size differs from {} bytes\n", path, value.size());
            return false;
        }

        std::memcpy(bytes.data() + member->Offset(), value.data(), value.size());
        return true;
    }

    std::vector<Element> DownsampleElements(std::span<const Element> elements)
    {
        std::vector<Element> result(static_cast<size_t>(k_ImageSize) * static_cast<size_t>(k_ImageSize));
        const float weight = 1.0f / static_cast<float>(k_Downsample * k_Downsample);

        for (int32_t y = 0; y < k_ImageSize; ++y)
        {
            for (int32_t x = 0; x < k_ImageSize; ++x)
            {
                Element sum{};

                for (int32_t offsetY = 0; offsetY < k_Downsample; ++offsetY)
                {
                    for (int32_t offsetX = 0; offsetX < k_Downsample; ++offsetX)
                    {
                        const int32_t sourceX = x * k_Downsample + offsetX;
                        const int32_t sourceY = y * k_Downsample + offsetY;
                        const Element& source = elements[static_cast<size_t>(sourceY * k_RenderSize + sourceX)];

                        for (size_t channel = 0; channel < sum.size(); ++channel)
                        {
                            sum[channel] += source[channel] * weight;
                        }
                    }
                }

                result[static_cast<size_t>(y * k_ImageSize + x)] = sum;
            }
        }

        return result;
    }

    bool WriteHeightPng(const std::filesystem::path& path, std::span<const Element> elements)
    {
        const auto heights = elements | std::views::transform([](const Element& element) { return element[0]; });
        const auto [lowest, highest] = std::ranges::minmax(heights);
        const float scale = highest > lowest ? 1.0f / (highest - lowest) : 0.0f;

        std::vector<uint8_t> pixels(elements.size());

        for (size_t index = 0; index < elements.size(); ++index)
        {
            const float unit = (elements[index][0] - lowest) * scale;
            pixels[index] = static_cast<uint8_t>(std::lround(std::clamp(unit, 0.0f, 1.0f) * 255.0f));
        }

        return stbi_write_png(path.string().c_str(), k_ImageSize, k_ImageSize, 1, pixels.data(), k_ImageSize) != 0;
    }

    bool WriteShadedPng(const std::filesystem::path& path, std::span<const Element> elements)
    {
        const auto heights = elements | std::views::transform([](const Element& element) { return element[0]; });
        const auto [lowest, highest] = std::ranges::minmax(heights);
        const float heightScale = highest > lowest ? 1.0f / (highest - lowest) : 0.0f;

        // relief from the gradient that the kernel wrote. The vertical exaggeration puts the 90th percentile slope at
        // about 45 degrees, so that every rule reads, whatever its gradient magnitude
        std::vector<float> slopes(elements.size());

        for (size_t index = 0; index < elements.size(); ++index)
        {
            slopes[index] = std::hypot(elements[index][1], elements[index][2]);
        }

        std::vector<float> sortedSlopes = slopes;
        const auto percentile = sortedSlopes.begin() + static_cast<std::ptrdiff_t>(sortedSlopes.size() * 9 / 10);
        std::ranges::nth_element(sortedSlopes, percentile);
        const float exaggeration = *percentile > 0.0f ? 1.0f / *percentile : 1.0f;

        // light from the north-west, 45 degrees up
        const std::array<float, 3> light{ -0.5f, -0.5f, 0.70710678f };
        std::vector<uint8_t> pixels(elements.size() * 3u);

        for (size_t index = 0; index < elements.size(); ++index)
        {
            const float slopeX = elements[index][1] * exaggeration;
            const float slopeY = elements[index][2] * exaggeration;
            const float normalLength = std::sqrt(slopeX * slopeX + slopeY * slopeY + 1.0f);
            const float lambert = (-slopeX * light[0] - slopeY * light[1] + light[2]) / normalLength;
            const float shade = 0.25f + 0.75f * std::max(lambert, 0.0f);

            const std::array<float, 3> color = TerrainColor((elements[index][0] - lowest) * heightScale);

            for (size_t channel = 0; channel < 3u; ++channel)
            {
                const float linear = std::clamp(color[channel] * shade, 0.0f, 1.0f);
                const float encoded = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
                pixels[index * 3u + channel] = static_cast<uint8_t>(std::lround(encoded * 255.0f));
            }
        }

        return stbi_write_png(path.string().c_str(), k_ImageSize, k_ImageSize, 3, pixels.data(), k_ImageSize * 3) != 0;
    }

    std::array<float, 3> TerrainColor(float height)
    {
        // linear colors: lowland green, olive, brown rock, grey rock, snow
        constexpr std::array<std::array<float, 4>, 5> k_Stops
        {
            std::array<float, 4>{ 0.00f, 0.040f, 0.090f, 0.030f },
            std::array<float, 4>{ 0.35f, 0.120f, 0.160f, 0.050f },
            std::array<float, 4>{ 0.60f, 0.220f, 0.140f, 0.070f },
            std::array<float, 4>{ 0.82f, 0.300f, 0.290f, 0.280f },
            std::array<float, 4>{ 1.00f, 0.850f, 0.870f, 0.900f },
        };

        for (size_t stop = 1; stop < k_Stops.size(); ++stop)
        {
            if (height <= k_Stops[stop][0])
            {
                const std::array<float, 4>& lower = k_Stops[stop - 1u];
                const std::array<float, 4>& upper = k_Stops[stop];
                const float t = (height - lower[0]) / (upper[0] - lower[0]);
                return { std::lerp(lower[1], upper[1], t), std::lerp(lower[2], upper[2], t), std::lerp(lower[3], upper[3], t) };
            }
        }

        const std::array<float, 4>& last = k_Stops.back();
        return { last[1], last[2], last[3] };
    }

    GradientCheck CheckGradient(std::span<const Element> elements, float spacing)
    {
        double errorSquared = 0.0;
        double gradientSquared = 0.0;
        std::vector<double> relativeErrors;
        relativeErrors.reserve(elements.size());

        for (int32_t y = 1; y < k_CheckSize - 1; ++y)
        {
            for (int32_t x = 1; x < k_CheckSize - 1; ++x)
            {
                const Element& center = elements[static_cast<size_t>(y * k_CheckSize + x)];
                const Element& left = elements[static_cast<size_t>(y * k_CheckSize + x - 1)];
                const Element& right = elements[static_cast<size_t>(y * k_CheckSize + x + 1)];
                const Element& up = elements[static_cast<size_t>((y - 1) * k_CheckSize + x)];
                const Element& down = elements[static_cast<size_t>((y + 1) * k_CheckSize + x)];

                const double differenceX = (static_cast<double>(right[0]) - left[0]) / (2.0 * spacing);
                const double differenceY = (static_cast<double>(down[0]) - up[0]) / (2.0 * spacing);
                const double errorX = differenceX - center[1];
                const double errorY = differenceY - center[2];
                const double gradientLength = std::hypot(static_cast<double>(center[1]), static_cast<double>(center[2]));
                const double errorLength = std::hypot(errorX, errorY);

                errorSquared += errorLength * errorLength;
                gradientSquared += gradientLength * gradientLength;
                relativeErrors.push_back(errorLength / std::max(gradientLength, 1.0e-6));
            }
        }

        const auto median = relativeErrors.begin() + static_cast<std::ptrdiff_t>(relativeErrors.size() / 2u);
        std::ranges::nth_element(relativeErrors, median);
        return GradientCheck{ std::sqrt(errorSquared / std::max(gradientSquared, 1.0e-30)), *median };
    }
}
