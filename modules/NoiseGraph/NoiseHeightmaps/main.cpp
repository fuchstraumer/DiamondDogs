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
#include "ShaderBundle.hpp"
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
    constexpr int32_t k_ImageSize = 16384;
    constexpr int32_t k_Downsample = k_RenderSize / k_ImageSize;
    constexpr float k_RenderExtent = 5.0f;

    // 2048 rows of 16384 float4 elements are 512 MiB, one band
    constexpr int32_t k_BandRows = 2048;

    // fine enough that the finest octave spans about 39 elements, so central differences are accurate
    constexpr int32_t k_CheckSize = 256;
    constexpr float k_CheckSpacing = 0.0002f;
    constexpr std::array<float, 2> k_CheckOrigin{ 1.37f, 2.71f };

    constexpr uint64_t k_TimeoutNs = 10'000'000'000ull;

    /// A cooked module, the one entry point to dispatch from it, its parameter block, and the member of that block that
    /// holds the field.
    struct KernelName
    {
        std::string_view Module;
        std::array<std::string_view, 1> EntryPoints;
        std::string_view ParameterBlock;
        std::string_view FieldMember;
    };

    constexpr KernelName k_FractalKernel{ "NoiseKernels", { "NoiseFractalCS" }, "gFractalNode", "fractal" };
    constexpr KernelName k_WarpStudyKernel{ "NoiseWarpStudy", { "WarpJacobianCS" }, "gFractalNode", "fractal" };
    constexpr KernelName k_CellularKernel{ "NoiseCellularKernels", { "NoiseCellularCS" }, "gCellularNode", "cellular" };

    // the cellular samples render at this size directly, with no downsample
    constexpr int32_t k_CellularImageSize = 2048;
    constexpr int32_t k_MaxRuleParameters = 7;

    // the places of Swiss's two optional changes in its RuleParameters
    constexpr size_t k_SwissFalloffParameter = 3u;
    constexpr size_t k_SwissSmoothClampParameter = 4u;

    // at 16384 texels over 5 units, 12 octaves put the finest wavelength at about 3 texels
    constexpr int32_t k_DetailOctaves = 12;

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

    /// The color ramp of the shaded relief.
    enum class Palette
    {
        Terrain,
        Mars,
    };

    /// A run of whole rows of a GridArea, dispatched and read back as one unit.
    struct GridBand
    {
        int32_t FirstRow;
        int32_t RowCount;
    };

    /// The objects that every band of one RenderArea call shares.
    struct BandTarget
    {
        const rhi::ManifestProgram& Program;
        const lodestone::manifest::ResolvedResource& Parameters;
        std::string_view FieldMember;
        uint32_t ParameterSet;
        uint32_t OutputSet;
        std::array<VkDescriptorSet, 2> DescriptorSets;
        GraphicsResource OutputBuffer;
        GraphicsResource ReadbackBuffer;
    };

    /// What WarpJacobianCS found over a window.
    struct JacobianSummary
    {
        double FoldedPercent;
        double NearSingularPercent;
        float SmallestSingular;
        float SmallestDeterminant;
        float AnisotropyP99;
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
                         { RuleParameter{ "gain", 0.5f }, RuleParameter{ "warpStrength", 0.15f }, RuleParameter{ "compoundingWarp", 0.0f },
                           RuleParameter{ "warpFalloff", 0.0f }, RuleParameter{ "smoothClamp", 0.0f } }, 5 },
        HeightmapSample{ "swiss_perlin_compounding", "Perlin3", "Swiss", 1.0f, 2.0f, 8, 7u, 1,
                         { RuleParameter{ "gain", 0.5f }, RuleParameter{ "warpStrength", 0.02f }, RuleParameter{ "compoundingWarp", 1.0f },
                           RuleParameter{ "warpFalloff", 0.0f }, RuleParameter{ "smoothClamp", 0.0f } }, 5 },
        HeightmapSample{ "jordan_perlin", "Perlin3", "Jordan", 1.0f, 2.0f, 8, 7u, 1,
                         { RuleParameter{ "gain1", 0.8f }, RuleParameter{ "gain", 0.5f }, RuleParameter{ "warp0", 0.4f },
                           RuleParameter{ "warp", 0.35f }, RuleParameter{ "damp0", 1.0f }, RuleParameter{ "damp", 0.8f },
                           RuleParameter{ "dampScale", 1.0f } }, 7 },
    };

    // Cellular3 samples: no basis or rule, so no variant axes. The "rule parameters" are Cellular3's own members. The
    // metric and output values are the k_Cellular constants of NoiseCellular.slang
    const std::array<HeightmapSample, 6> k_CellularSamples
    {
        HeightmapSample{ "cellular_f1_euclidean", "", "", 4.0f, 0.0f, 0, 7u, 0,
                         { RuleParameter{ "jitter", 1.0f }, RuleParameter{ "density", 1.0f }, RuleParameter{ "metric", 0.0f },
                           RuleParameter{ "minkowskiExponent", 2.0f }, RuleParameter{ "output", 0.0f } }, 5 },
        HeightmapSample{ "cellular_f2minusf1_euclidean", "", "", 4.0f, 0.0f, 0, 7u, 0,
                         { RuleParameter{ "jitter", 1.0f }, RuleParameter{ "density", 1.0f }, RuleParameter{ "metric", 0.0f },
                           RuleParameter{ "minkowskiExponent", 2.0f }, RuleParameter{ "output", 2.0f } }, 5 },
        HeightmapSample{ "cellular_f1_manhattan_density2", "", "", 4.0f, 0.0f, 0, 7u, 0,
                         { RuleParameter{ "jitter", 1.0f }, RuleParameter{ "density", 2.0f }, RuleParameter{ "metric", 2.0f },
                           RuleParameter{ "minkowskiExponent", 2.0f }, RuleParameter{ "output", 0.0f } }, 5 },
        HeightmapSample{ "cellular_f1_chebyshev_jitter05", "", "", 4.0f, 0.0f, 0, 7u, 0,
                         { RuleParameter{ "jitter", 0.5f }, RuleParameter{ "density", 1.0f }, RuleParameter{ "metric", 3.0f },
                           RuleParameter{ "minkowskiExponent", 2.0f }, RuleParameter{ "output", 0.0f } }, 5 },
        HeightmapSample{ "cellular_f2_minkowski05", "", "", 4.0f, 0.0f, 0, 7u, 0,
                         { RuleParameter{ "jitter", 1.0f }, RuleParameter{ "density", 1.0f }, RuleParameter{ "metric", 4.0f },
                           RuleParameter{ "minkowskiExponent", 0.5f }, RuleParameter{ "output", 1.0f } }, 5 },
        HeightmapSample{ "cellular_cellvalue", "", "", 4.0f, 0.0f, 0, 7u, 0,
                         { RuleParameter{ "jitter", 1.0f }, RuleParameter{ "density", 1.0f }, RuleParameter{ "metric", 0.0f },
                           RuleParameter{ "minkowskiExponent", 2.0f }, RuleParameter{ "output", 5.0f } }, 5 },
    };

    [[nodiscard]] std::optional<std::vector<Element>> RenderArea(rhi::RhiSystem& rhi_system,
                                                                 ResourceContext& resources,
                                                                 const KernelName& kernel,
                                                                 const rhi::ShaderLibrary& library,
                                                                 const HeightmapSample& sample,
                                                                 const GridArea& area);
    [[nodiscard]] bool RenderBand(rhi::Device& device,
                                  ResourceContext& resources,
                                  const HeightmapSample& sample,
                                  const GridArea& area,
                                  const GridBand& band,
                                  const BandTarget& target,
                                  std::span<Element> result);
    [[nodiscard]] bool WriteParameters(std::span<std::byte> bytes,
                                       lodestone::manifest::UniformMemberRange members,
                                       const HeightmapSample& sample,
                                       const GridArea& area,
                                       const GridBand& band,
                                       std::string_view field_member);
    [[nodiscard]] bool WriteMember(std::span<std::byte> bytes,
                                   lodestone::manifest::UniformMemberRange members,
                                   std::string_view path,
                                   std::span<const std::byte> value);
    [[nodiscard]] int32_t RunRegionStudy(rhi::RhiSystem& rhi_system,
                                         ResourceContext& resources,
                                         const rhi::ShaderLibrary& library,
                                         const std::filesystem::path& output_folder,
                                         int32_t center_x,
                                         int32_t center_y,
                                         int32_t size);
    [[nodiscard]] int32_t RenderCellularSamples(rhi::RhiSystem& rhi_system,
                                                ResourceContext& resources,
                                                const rhi::ShaderLibrary& library,
                                                const std::filesystem::path& output_folder);
    [[nodiscard]] bool WriteJacobianPng(const std::filesystem::path& path, std::span<const Element> elements, int32_t size);
    [[nodiscard]] JacobianSummary SummarizeJacobian(std::span<const Element> elements);
    [[nodiscard]] std::vector<Element> DownsampleElements(std::span<const Element> elements);
    [[nodiscard]] bool WriteHeightPng(const std::filesystem::path& path, std::span<const Element> elements, int32_t size);
    [[nodiscard]] bool WriteShadedPng(const std::filesystem::path& path, std::span<const Element> elements, int32_t size, Palette palette);
    [[nodiscard]] std::array<float, 3> TerrainColor(float height, Palette palette);
    [[nodiscard]] GradientCheck CheckGradient(std::span<const Element> elements, float spacing);
}

int main(int argc, char** argv)
{
    const bool regionStudy = argc == 6 && std::string_view{ argv[2] } == "--region";
    const bool detail = argc == 3 && std::string_view{ argv[2] } == "--detail";
    const bool mars = argc == 3 && std::string_view{ argv[2] } == "--mars";
    const bool cellular = argc == 3 && std::string_view{ argv[2] } == "--cellular";
    const Palette palette = mars ? Palette::Mars : Palette::Terrain;
    const std::string_view fileSuffix = mars ? "_mars" : "";

    if (argc != 2 && !regionStudy && !detail && !mars && !cellular)
    {
        std::cerr << "Usage: NoiseHeightmaps <output folder> [--region <x> <y> <size> | --detail | --mars | --cellular]\n";
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

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(NOISE_HEIGHTMAPS_MANIFEST, rhi::k_SpirvProfileName);

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

        if (regionStudy)
        {
            const int32_t centerX = std::stoi(argv[3]);
            const int32_t centerY = std::stoi(argv[4]);
            const int32_t size = std::stoi(argv[5]);
            failures += RunRegionStudy(*rhiSystem, resources, **library, outputFolder, centerX, centerY, size);
        }

        if (cellular)
        {
            failures += RenderCellularSamples(*rhiSystem, resources, **library, outputFolder);
        }

        const bool fractalSamples = !regionStudy && !cellular;

        for (HeightmapSample sample : fractalSamples ? std::span{ k_Samples } : std::span<const HeightmapSample>{})
        {
            // --detail renders only Swiss and Jordan, at k_DetailOctaves, and Swiss with warpFalloff and smoothClamp on
            if (detail && sample.Rule != "Swiss" && sample.Rule != "Jordan")
            {
                continue;
            }

            // --mars renders only Jordan, at k_DetailOctaves, in the Mars palette
            if (mars && sample.Rule != "Jordan")
            {
                continue;
            }

            if (detail || mars)
            {
                sample.OctaveCount = k_DetailOctaves;
            }

            if (detail && sample.Rule == "Swiss")
            {
                sample.RuleParameters[k_SwissFalloffParameter].Value = 1.0f;
                sample.RuleParameters[k_SwissSmoothClampParameter].Value = 1.0f;
            }

            std::optional<std::vector<Element>> rendered = RenderArea(*rhiSystem, resources, k_FractalKernel, **library, sample, renderArea);
            std::optional<std::vector<Element>> checked = RenderArea(*rhiSystem, resources, k_FractalKernel, **library, sample, checkArea);

            if (!rendered.has_value() || !checked.has_value())
            {
                ++failures;
                continue;
            }

            const std::vector<Element> image = DownsampleElements(*rendered);
            const bool wroteHeight = WriteHeightPng(outputFolder / std::format("{}{}_height.png", sample.Name, fileSuffix), image, k_ImageSize);
            const bool wroteShaded = WriteShadedPng(outputFolder / std::format("{}{}_shaded.png", sample.Name, fileSuffix), image, k_ImageSize, palette);

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

    std::cout << std::format("NoiseHeightmaps: wrote the images to {}\n", outputFolder.string());
    return 0;
}

namespace
{
    std::optional<std::vector<Element>> RenderArea(rhi::RhiSystem& rhi_system,
                                                   ResourceContext& resources,
                                                   const KernelName& kernel,
                                                   const rhi::ShaderLibrary& library,
                                                   const HeightmapSample& sample,
                                                   const GridArea& area)
    {
        rhi::Device& device = *rhi_system.GetDevice();
        const VkDevice vkDevice = device.Handle().As<VkDevice>();
        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(kernel.Module);

        if (!index.has_value())
        {
            std::cerr << std::format("[{}] module {} not found: {}\n", sample.Name, kernel.Module, rhi::ToString(index.error()));
            return std::nullopt;
        }

        // a sample with no basis is a module with no variant axes
        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = sample.Basis.empty()
            ? (*index)->Query().SingleFor(kernel.EntryPoints)
            : (*index)->Query()
                  .Where("BASIS", lodestone::AxisValueDomain::Type, sample.Basis)
                  .Where("RULE", lodestone::AxisValueDomain::Type, sample.Rule)
                  .SingleFor(kernel.EntryPoints);

        if (!variant.has_value())
        {
            std::cerr << std::format("[{}] variant query failed, code {}, axis '{}'\n",
                                     sample.Name, static_cast<int32_t>(variant.error().Code), variant.error().AxisName);
            return std::nullopt;
        }

        const lodestone::manifest::EnvironmentView& environment = (*index)->View();
        lodestone::manifest::ManifestResult<uint32_t> entryPointIndex = environment.Module().FindEntryPointIndex(kernel.EntryPoints[0]);
        std::optional<lodestone::manifest::VariantView> variantView = environment.VariantByKey(*variant);

        if (!entryPointIndex.has_value() || !variantView.has_value())
        {
            std::cerr << std::format("[{}] the manifest has no {} for this variant\n", sample.Name, kernel.EntryPoints[0]);
            return std::nullopt;
        }

        // the node parameters and the output, by name; group and binding come from the manifest
        std::optional<lodestone::manifest::ResolvedResource> parameters;
        std::optional<lodestone::manifest::ResolvedResource> output;
        const lodestone::manifest::EntryPointInstanceView instance = variantView->EntryPoint(*entryPointIndex);

        for (const lodestone::manifest::ResolvedResource resource : instance.Layout())
        {
            if (resource.Name() == kernel.ParameterBlock)
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
            std::cerr << std::format("[{}] the variant does not bind {} and gNodeOutput\n", sample.Name, kernel.ParameterBlock);
            return std::nullopt;
        }

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program =
            library.CreateProgram(device.Handle(), kernel.Module, *variant, kernel.EntryPoints);

        if (!program.has_value())
        {
            std::cerr << std::format("[{}] program creation failed: {}\n", sample.Name, rhi::ToString(program.error()));
            return std::nullopt;
        }

        // one output buffer and one readback buffer hold one band of rows. The bands run one after another, so memory
        // stays fixed at any image size, and no buffer passes maxStorageBufferRange (4 GiB less one byte on the 4080)
        const int32_t bandRows = std::min(area.Size, k_BandRows);
        const size_t bandBytes = static_cast<size_t>(area.Size) * static_cast<size_t>(bandRows) * sizeof(Element);

        BufferCreateInfo outputInfo{};
        outputInfo.Domain = ResourceDomain::GPUOnly;
        outputInfo.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource;
        outputInfo.Size = bandBytes;
        rhi::ResourceFuture<GraphicsResource> outputReply = resources.CreateBuffer(outputInfo, {});

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::CopyDestination;
        readbackInfo.Size = bandBytes;
        rhi::ResourceFuture<GraphicsResource> readbackReply = resources.CreateBuffer(readbackInfo, {});

        if (resources.Wait(outputReply, k_TimeoutNs) != rhi::FutureState::Complete ||
            resources.Wait(readbackReply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            std::cerr << std::format("[{}] ResourceContext did not create the band buffers\n", sample.Name);
            return std::nullopt;
        }

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

        const VkDescriptorBufferInfo outputDescriptor{ reinterpret_cast<VkBuffer>(outputBuffer.VkHandle), 0, VK_WHOLE_SIZE };
        const VkWriteDescriptorSet outputWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSets[1], output->PlacementValue().Word1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputDescriptor, nullptr };
        vkUpdateDescriptorSets(vkDevice, 1, &outputWrite, 0, nullptr);

        const BandTarget target{ **program, *parameters, kernel.FieldMember, parameterSet, outputSet, descriptorSets, outputBuffer, readbackBuffer };
        std::vector<Element> result(static_cast<size_t>(area.Size) * static_cast<size_t>(area.Size));
        bool bandsComplete = true;

        for (int32_t firstRow = 0; firstRow < area.Size && bandsComplete; firstRow += bandRows)
        {
            const GridBand band{ firstRow, std::min(bandRows, area.Size - firstRow) };
            bandsComplete = RenderBand(device, resources, sample, area, band, target, result);
        }

        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);

        for (const GraphicsResource& resource : { outputBuffer, readbackBuffer })
        {
            rhi::OperationFuture destroyReply = resources.DestroyResource(resource);
            (void)resources.Wait(destroyReply, k_TimeoutNs);
        }

        if (!bandsComplete)
        {
            std::cerr << std::format("[{}] a band failed to dispatch or to read back\n", sample.Name);
            return std::nullopt;
        }

        return result;
    }

    bool RenderBand(rhi::Device& device,
                    ResourceContext& resources,
                    const HeightmapSample& sample,
                    const GridArea& area,
                    const GridBand& band,
                    const BandTarget& target,
                    std::span<Element> result)
    {
        std::vector<std::byte> parameterBytes(static_cast<size_t>(target.Parameters.ByteSize()));

        if (!WriteParameters(parameterBytes, target.Parameters.Members(), sample, area, band, target.FieldMember))
        {
            return false;
        }

        const RhiBufferResourceData parameterData{ std::span<const std::byte>{ parameterBytes }, 0u };
        BufferCreateInfo parameterInfo{};
        parameterInfo.Domain = ResourceDomain::CPUToGPU;
        parameterInfo.Usage = BufferUsageBits::ComputeShaderUBO;
        parameterInfo.Size = parameterBytes.size();
        rhi::ResourceFuture<GraphicsResource> parameterReply = resources.CreateBuffer(parameterInfo, std::span{ &parameterData, 1u });

        if (resources.Wait(parameterReply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            return false;
        }

        const GraphicsResource parameterBuffer = parameterReply.Get();

        // the previous band's fence was waited on, so its parameter set is free to point at the new buffer
        const VkDescriptorBufferInfo parameterDescriptor{ reinterpret_cast<VkBuffer>(parameterBuffer.VkHandle), 0, VK_WHOLE_SIZE };
        const VkWriteDescriptorSet parameterWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, target.DescriptorSets[0], target.Parameters.PlacementValue().Word1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &parameterDescriptor, nullptr };
        vkUpdateDescriptorSets(device.Handle().As<VkDevice>(), 1, &parameterWrite, 0, nullptr);

        const size_t bandBytes = static_cast<size_t>(area.Size) * static_cast<size_t>(band.RowCount) * sizeof(Element);
        bool dispatched = false;

        {
            rhi::CommandPool commandPool(device.Handle(), rhi::CommandPool::Type::Compute, device.GetQueueFamilyIndices().Graphics);
            RhiAssert(commandPool.AllocateCommandBuffers(1));
            rhi::CommandBuffer commandBuffer(commandPool.GetCommandBuffer(0));
            RhiAssert(commandBuffer.Begin());

            // the previous band's copy read the output buffer, and this band's dispatch writes it again
            const rhi::BufferHandle outputHandle{ target.OutputBuffer.VkHandle };
            const rhi::BufferBarrier toWrite
            {
                outputHandle,
                rhi::PipelineStage::Copy, rhi::Access::TransferRead,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite
            };
            commandBuffer.Barrier(rhi::DependencyInfo{ {}, std::span{ &toWrite, 1 }, {} });
            RhiAssert(commandBuffer.BindShaders(target.Program.Program()));

            const rhi::DescriptorSetHandle parameterSetHandle{ reinterpret_cast<uint64_t>(target.DescriptorSets[0]) };
            const rhi::DescriptorSetHandle outputSetHandle{ reinterpret_cast<uint64_t>(target.DescriptorSets[1]) };
            commandBuffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, target.Program.PipelineLayout(), target.ParameterSet, std::span{ &parameterSetHandle, 1 });
            commandBuffer.BindDescriptorSets(rhi::PipelineBindPoint::Compute, target.Program.PipelineLayout(), target.OutputSet, std::span{ &outputSetHandle, 1 });

            const std::array<uint32_t, 3> workgroup = target.Program.Workgroup();
            const uint32_t width = static_cast<uint32_t>(area.Size);
            const uint32_t rows = static_cast<uint32_t>(band.RowCount);
            commandBuffer.Dispatch((width + workgroup[0] - 1u) / workgroup[0], (rows + workgroup[1] - 1u) / workgroup[1], 1);

            const rhi::BufferBarrier toCopy
            {
                outputHandle,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite,
                rhi::PipelineStage::Copy, rhi::Access::TransferRead
            };
            commandBuffer.Barrier(rhi::DependencyInfo{ {}, std::span{ &toCopy, 1 }, {} });
            const rhi::BufferCopy copyRegion{ 0u, 0u, bandBytes };
            commandBuffer.CopyBuffer(outputHandle, rhi::BufferHandle{ target.ReadbackBuffer.VkHandle }, std::span{ &copyRegion, 1 });
            const rhi::GlobalBarrier toHost{ rhi::PipelineStage::Copy, rhi::Access::TransferWrite, rhi::PipelineStage::Host, rhi::Access::HostRead };
            commandBuffer.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1 }, {}, {} });
            RhiAssert(commandBuffer.End());

            const rhi::CommandBufferHandle commandBufferHandle = commandBuffer.Handle();
            rhi::Fence fence(device.Handle());
            RhiAssert(rhi::QueueSubmit(device.GetGeneralQueue(), rhi::SubmitInfo{ {}, std::span{ &commandBufferHandle, 1 }, {} }, fence.Handle()));
            dispatched = fence.Wait();
        }

        bool readBack = false;

        if (dispatched)
        {
            rhi::ResourceFuture<void*> mapReply = resources.MapBuffer(target.ReadbackBuffer, bandBytes, 0u);

            if (resources.Wait(mapReply, k_TimeoutNs) == rhi::FutureState::Complete)
            {
                const size_t firstElement = static_cast<size_t>(band.FirstRow) * static_cast<size_t>(area.Size);
                std::memcpy(result.data() + firstElement, mapReply.Get(), bandBytes);
                rhi::OperationFuture unmapReply = resources.UnmapBuffer(target.ReadbackBuffer, bandBytes, 0u);
                (void)resources.Wait(unmapReply, k_TimeoutNs);
                readBack = true;
            }
        }

        rhi::OperationFuture destroyReply = resources.DestroyResource(parameterBuffer);
        (void)resources.Wait(destroyReply, k_TimeoutNs);
        return readBack;
    }

    bool WriteParameters(std::span<std::byte> bytes,
                         lodestone::manifest::UniformMemberRange members,
                         const HeightmapSample& sample,
                         const GridArea& area,
                         const GridBand& band,
                         std::string_view field_member)
    {
        // the band's origin is computed in double, so a band's first row sits where it would sit in one large grid
        const double bandOriginY = static_cast<double>(area.Origin[1]) + static_cast<double>(band.FirstRow) * static_cast<double>(area.Spacing);
        const std::array<float, 2> origin{ area.Origin[0], static_cast<float>(bandOriginY) };
        const std::array<float, 2> spacing{ area.Spacing, area.Spacing };
        const std::array<int32_t, 2> extent{ area.Size, band.RowCount };

        bool wrote = WriteMember(bytes, members, "grid.origin", std::as_bytes(std::span{ origin }));
        wrote = wrote && WriteMember(bytes, members, "grid.spacing", std::as_bytes(std::span{ spacing }));
        wrote = wrote && WriteMember(bytes, members, "grid.extent", std::as_bytes(std::span{ extent }));
        wrote = wrote && WriteMember(bytes, members, std::format("{}.frequency", field_member), std::as_bytes(std::span{ &sample.Frequency, 1 }));
        wrote = wrote && WriteMember(bytes, members, std::format("{}.seed", field_member), std::as_bytes(std::span{ &sample.Seed, 1 }));

        // a fractal has the octave members, and its rule's parameters sit in its rule member
        const bool fractal = !sample.Rule.empty();

        if (fractal)
        {
            wrote = wrote && WriteMember(bytes, members, "fractal.lacunarity", std::as_bytes(std::span{ &sample.Lacunarity, 1 }));
            wrote = wrote && WriteMember(bytes, members, "fractal.octaveCount", std::as_bytes(std::span{ &sample.OctaveCount, 1 }));
            wrote = wrote && WriteMember(bytes, members, "fractal.rotateOctaves", std::as_bytes(std::span{ &sample.RotateOctaves, 1 }));
        }

        const std::string parameterRoot = fractal ? std::string{ "fractal.rule" } : std::string{ field_member };

        for (int32_t index = 0; index < sample.RuleParameterCount; ++index)
        {
            const RuleParameter& parameter = sample.RuleParameters[static_cast<size_t>(index)];
            const std::string path = std::format("{}.{}", parameterRoot, parameter.Name);

            // Swiss's flags and Cellular3's metric and output are the only integer parameters
            const bool isFlag = parameter.Name == "compoundingWarp" || parameter.Name == "warpFalloff" || parameter.Name == "smoothClamp" ||
                                parameter.Name == "metric" || parameter.Name == "output";

            if (isFlag)
            {
                const int32_t flag = static_cast<int32_t>(parameter.Value);
                wrote = wrote && WriteMember(bytes, members, path, std::as_bytes(std::span{ &flag, 1 }));
                continue;
            }

            wrote = wrote && WriteMember(bytes, members, path, std::as_bytes(std::span{ &parameter.Value, 1 }));
        }

        if (!wrote)
        {
            std::cerr << std::format("[{}] a parameter path is missing under '{}'\n", sample.Name, field_member);
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
                        const size_t sourceX = static_cast<size_t>(x * k_Downsample + offsetX);
                        const size_t sourceY = static_cast<size_t>(y * k_Downsample + offsetY);
                        const Element& source = elements[sourceY * static_cast<size_t>(k_RenderSize) + sourceX];

                        for (size_t channel = 0; channel < sum.size(); ++channel)
                        {
                            sum[channel] += source[channel] * weight;
                        }
                    }
                }

                result[static_cast<size_t>(y) * static_cast<size_t>(k_ImageSize) + static_cast<size_t>(x)] = sum;
            }
        }

        return result;
    }

    int32_t RunRegionStudy(rhi::RhiSystem& rhi_system,
                           ResourceContext& resources,
                           const rhi::ShaderLibrary& library,
                           const std::filesystem::path& output_folder,
                           int32_t center_x,
                           int32_t center_y,
                           int32_t size)
    {
        // a window at the spacing of the full 16384 render, centered on an image pixel (x right, y down), for 8 and 12
        // octaves: de Carpentier's Swiss at warp 0.15, each of our two changes alone, both, and Jordan as published
        struct StudyVariant
        {
            std::string_view Name;
            size_t SampleIndex;
            float WarpFalloff;
            float SmoothClamp;
        };

        constexpr size_t k_SwissSample = 2u;
        constexpr size_t k_JordanSample = 4u;
        constexpr std::array<StudyVariant, 5> k_Variants
        {
            StudyVariant{ "original", k_SwissSample, 0.0f, 0.0f },
            StudyVariant{ "falloff", k_SwissSample, 1.0f, 0.0f },
            StudyVariant{ "smoothclamp", k_SwissSample, 0.0f, 1.0f },
            StudyVariant{ "both", k_SwissSample, 1.0f, 1.0f },
            StudyVariant{ "jordan", k_JordanSample, 0.0f, 0.0f },
        };
        constexpr std::array<int32_t, 2> k_OctaveCounts{ 8, 12 };

        const float spacing = k_RenderExtent / static_cast<float>(k_RenderSize);
        const int32_t firstX = center_x - size / 2;
        const int32_t firstY = center_y - size / 2;
        const GridArea window{ { static_cast<float>(firstX) * spacing, static_cast<float>(firstY) * spacing }, spacing, size };
        int32_t failures = 0;

        for (const StudyVariant& variant : k_Variants)
        {
            for (const int32_t octaveCount : k_OctaveCounts)
            {
                HeightmapSample sample = k_Samples[variant.SampleIndex];
                sample.OctaveCount = octaveCount;

                if (variant.SampleIndex == k_SwissSample)
                {
                    sample.RuleParameters[k_SwissFalloffParameter].Value = variant.WarpFalloff;
                    sample.RuleParameters[k_SwissSmoothClampParameter].Value = variant.SmoothClamp;
                }

                std::optional<std::vector<Element>> rendered = RenderArea(rhi_system, resources, k_FractalKernel, library, sample, window);

                if (!rendered.has_value())
                {
                    ++failures;
                    continue;
                }

                const std::string name = std::format("region_{}_oct{}", variant.Name, octaveCount);
                const bool wrote = WriteShadedPng(output_folder / std::format("{}.png", name), *rendered, size, Palette::Terrain);

                if (!wrote)
                {
                    ++failures;
                }

                std::optional<std::vector<Element>> jacobian = RenderArea(rhi_system, resources, k_WarpStudyKernel, library, sample, window);

                if (!jacobian.has_value() || !WriteJacobianPng(output_folder / std::format("{}_jacobian.png", name), *jacobian, size))
                {
                    ++failures;
                    continue;
                }

                const JacobianSummary summary = SummarizeJacobian(*jacobian);
                std::cout << std::format("[{}] folded {:.3f}%, singular value < 0.1: {:.3f}%, smallest singular {:.4f}, "
                                         "smallest det {:.4f}, anisotropy p99 {:.1f}\n",
                                         name, summary.FoldedPercent, summary.NearSingularPercent, summary.SmallestSingular,
                                         summary.SmallestDeterminant, summary.AnisotropyP99);
            }
        }

        return failures;
    }

    int32_t RenderCellularSamples(rhi::RhiSystem& rhi_system,
                                  ResourceContext& resources,
                                  const rhi::ShaderLibrary& library,
                                  const std::filesystem::path& output_folder)
    {
        // the same 5 x 5 unit square as the fractal samples, at k_CellularImageSize: frequency 4 gives 20 cells across
        const float spacing = k_RenderExtent / static_cast<float>(k_CellularImageSize);
        const GridArea area{ { 0.0f, 0.0f }, spacing, k_CellularImageSize };
        const GridArea checkArea{ k_CheckOrigin, k_CheckSpacing, k_CheckSize };
        int32_t failures = 0;

        for (const HeightmapSample& sample : k_CellularSamples)
        {
            std::optional<std::vector<Element>> rendered = RenderArea(rhi_system, resources, k_CellularKernel, library, sample, area);
            std::optional<std::vector<Element>> checked = RenderArea(rhi_system, resources, k_CellularKernel, library, sample, checkArea);

            if (!rendered.has_value() || !checked.has_value())
            {
                ++failures;
                continue;
            }

            const bool wroteHeight = WriteHeightPng(output_folder / std::format("{}_height.png", sample.Name), *rendered, k_CellularImageSize);
            const bool wroteShaded = WriteShadedPng(output_folder / std::format("{}_shaded.png", sample.Name), *rendered, k_CellularImageSize, Palette::Terrain);

            if (!wroteHeight || !wroteShaded)
            {
                std::cerr << std::format("[{}] a PNG could not be written\n", sample.Name);
                ++failures;
            }

            const auto values = *rendered | std::views::transform([](const Element& element) { return element[0]; });
            const auto [lowest, highest] = std::ranges::minmax(values);
            const GradientCheck check = CheckGradient(*checked, k_CheckSpacing);
            std::cout << std::format("[{}] value range [{:.4f}, {:.4f}]; gradient vs central differences: relative RMS {:.4f}, median {:.4f}\n",
                                     sample.Name, lowest, highest, check.RelativeRmsError, check.MedianRelativeError);
        }

        return failures;
    }

    bool WriteJacobianPng(const std::filesystem::path& path, std::span<const Element> elements, int32_t size)
    {
        // gray is the smallest singular value over the octaves, 1 white (no distortion) to 0 black (infinite stretch).
        // Red marks a fold, where the 2D determinant is <= 0
        std::vector<uint8_t> pixels(elements.size() * 3u);

        for (size_t index = 0; index < elements.size(); ++index)
        {
            const Element& element = elements[index];
            const uint8_t gray = static_cast<uint8_t>(std::lround(std::clamp(element[0], 0.0f, 1.0f) * 255.0f));
            const bool folded = element[1] <= 0.0f;
            pixels[index * 3u + 0u] = folded ? uint8_t{ 255 } : gray;
            pixels[index * 3u + 1u] = folded ? uint8_t{ 0 } : gray;
            pixels[index * 3u + 2u] = folded ? uint8_t{ 0 } : gray;
        }

        return stbi_write_png(path.string().c_str(), size, size, 3, pixels.data(), size * 3) != 0;
    }

    JacobianSummary SummarizeJacobian(std::span<const Element> elements)
    {
        JacobianSummary summary{ 0.0, 0.0, 1.0e30f, 1.0e30f, 0.0f };
        std::vector<float> anisotropy(elements.size());
        size_t folded = 0;
        size_t nearSingular = 0;

        for (size_t index = 0; index < elements.size(); ++index)
        {
            const Element& element = elements[index];
            folded += element[1] <= 0.0f ? 1u : 0u;
            nearSingular += element[0] < 0.1f ? 1u : 0u;
            summary.SmallestSingular = std::min(summary.SmallestSingular, element[0]);
            summary.SmallestDeterminant = std::min(summary.SmallestDeterminant, element[1]);
            anisotropy[index] = element[2];
        }

        const auto percentile = anisotropy.begin() + static_cast<std::ptrdiff_t>(anisotropy.size() * 99 / 100);
        std::ranges::nth_element(anisotropy, percentile);
        summary.AnisotropyP99 = *percentile;
        summary.FoldedPercent = 100.0 * static_cast<double>(folded) / static_cast<double>(elements.size());
        summary.NearSingularPercent = 100.0 * static_cast<double>(nearSingular) / static_cast<double>(elements.size());
        return summary;
    }

    bool WriteHeightPng(const std::filesystem::path& path, std::span<const Element> elements, int32_t size)
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

        return stbi_write_png(path.string().c_str(), size, size, 1, pixels.data(), size) != 0;
    }

    bool WriteShadedPng(const std::filesystem::path& path, std::span<const Element> elements, int32_t size, Palette palette)
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

            const std::array<float, 3> color = TerrainColor((elements[index][0] - lowest) * heightScale, palette);

            for (size_t channel = 0; channel < 3u; ++channel)
            {
                const float linear = std::clamp(color[channel] * shade, 0.0f, 1.0f);
                const float encoded = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
                pixels[index * 3u + channel] = static_cast<uint8_t>(std::lround(encoded * 255.0f));
            }
        }

        return stbi_write_png(path.string().c_str(), size, size, 3, pixels.data(), size * 3) != 0;
    }

    std::array<float, 3> TerrainColor(float height, Palette palette)
    {
        // linear colors: lowland green, olive, brown rock, grey rock, snow
        constexpr std::array<std::array<float, 4>, 5> k_TerrainStops
        {
            std::array<float, 4>{ 0.00f, 0.040f, 0.090f, 0.030f },
            std::array<float, 4>{ 0.35f, 0.120f, 0.160f, 0.050f },
            std::array<float, 4>{ 0.60f, 0.220f, 0.140f, 0.070f },
            std::array<float, 4>{ 0.82f, 0.300f, 0.290f, 0.280f },
            std::array<float, 4>{ 1.00f, 0.850f, 0.870f, 0.900f },
        };

        // linear colors: dark basalt, rust, iron-oxide orange, butterscotch dust, pale ochre
        constexpr std::array<std::array<float, 4>, 5> k_MarsStops
        {
            std::array<float, 4>{ 0.00f, 0.050f, 0.018f, 0.010f },
            std::array<float, 4>{ 0.30f, 0.220f, 0.065f, 0.022f },
            std::array<float, 4>{ 0.55f, 0.430f, 0.160f, 0.055f },
            std::array<float, 4>{ 0.80f, 0.580f, 0.330f, 0.150f },
            std::array<float, 4>{ 1.00f, 0.740f, 0.560f, 0.370f },
        };

        const std::array<std::array<float, 4>, 5>& stops = palette == Palette::Mars ? k_MarsStops : k_TerrainStops;

        for (size_t stop = 1; stop < stops.size(); ++stop)
        {
            if (height <= stops[stop][0])
            {
                const std::array<float, 4>& lower = stops[stop - 1u];
                const std::array<float, 4>& upper = stops[stop];
                const float t = (height - lower[0]) / (upper[0] - lower[0]);
                return { std::lerp(lower[1], upper[1], t), std::lerp(lower[2], upper[2], t), std::lerp(lower[3], upper[3], t) };
            }
        }

        const std::array<float, 4>& last = stops.back();
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
