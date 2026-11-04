#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "Queue.hpp"
#include "QueryPool.hpp"
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderProgram.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Times the raster pass and the compute pass of a 3840x2160 target on the graphics queue. Each format runs in exclusive
// and in concurrent sharing mode. The images come from raw Vulkan, not ResourceContext, so that each case picks its own
// sharing mode. The timestamps come from one QueryPool, and each iteration waits for its own submission.

namespace
{
    // 8K, so that every target is at least twice the 64 MB L2 of an RTX 4080 and the passes reach DRAM, where compression
    // matters. at 4K, the RGBA8 and D32 targets fit in L2 and the passes measure only L2
    constexpr uint32_t k_Width = 7680u;
    constexpr uint32_t k_Height = 4320u;
    // one SumCS group covers 32x32 texels: 16x16 threads that read 2x2 texels each, so that few groups are launched
    constexpr uint32_t k_TileSize = 32u;
    constexpr uint32_t k_GroupsX = (k_Width + k_TileSize - 1u) / k_TileSize;
    constexpr uint32_t k_GroupsY = (k_Height + k_TileSize - 1u) / k_TileSize;
    constexpr uint32_t k_SumCount = k_GroupsX * k_GroupsY;
    constexpr VkDeviceSize k_SumBytes = sizeof(float) * k_SumCount;
    constexpr int32_t k_WarmupIterations = 20;
    constexpr int32_t k_TimedIterations = 200;
    constexpr uint32_t k_QueryCount = 4u;
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;
    constexpr std::string_view k_ModuleName{ "SharingBenchmark" };
    constexpr std::array<std::string_view, 2> k_ColorEntryPoints{ "VSMain", "ColorFS" };
    constexpr std::array<std::string_view, 2> k_DepthEntryPoints{ "VSMain", "DepthFS" };
    constexpr std::array<std::string_view, 1> k_ComputeEntryPoints{ "SumCS" };

    struct CaseSpec
    {
        std::string_view FormatName{};
        VkFormat Format{ VK_FORMAT_UNDEFINED };
        bool IsDepth{ false };
        bool Concurrent{ false };
    };

    constexpr std::array<CaseSpec, 6> k_Cases
    {
        CaseSpec{ "R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT, false, false },
        CaseSpec{ "R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT, false, true },
        CaseSpec{ "R8G8B8A8_UNORM", VK_FORMAT_R8G8B8A8_UNORM, false, false },
        CaseSpec{ "R8G8B8A8_UNORM", VK_FORMAT_R8G8B8A8_UNORM, false, true },
        CaseSpec{ "D32_SFLOAT", VK_FORMAT_D32_SFLOAT, true, false },
        CaseSpec{ "D32_SFLOAT", VK_FORMAT_D32_SFLOAT, true, true },
    };

    struct PassStatistics
    {
        double MedianUs{ 0.0 };
        double P90Us{ 0.0 };
        // the timer ticks in steps of about 1 us on this GPU; the mean of many samples resolves less than one step
        double MeanUs{ 0.0 };
    };

    struct CaseResult
    {
        CaseSpec Spec{};
        PassStatistics Raster{};
        PassStatistics Compute{};
        double Checksum{ 0.0 };
    };

    struct ComputeBindings
    {
        uint32_t SourceBinding{ 0u };
        uint32_t SumsBinding{ 0u };
    };

    struct ShaderObjectFunctions
    {
        PFN_vkCmdSetPrimitiveTopologyEXT SetPrimitiveTopology{ nullptr };
        PFN_vkCmdSetPrimitiveRestartEnableEXT SetPrimitiveRestartEnable{ nullptr };
        PFN_vkCmdSetVertexInputEXT SetVertexInput{ nullptr };
        PFN_vkCmdSetDepthClampEnableEXT SetDepthClampEnable{ nullptr };
        PFN_vkCmdSetRasterizerDiscardEnableEXT SetRasterizerDiscardEnable{ nullptr };
        PFN_vkCmdSetPolygonModeEXT SetPolygonMode{ nullptr };
        PFN_vkCmdSetCullModeEXT SetCullMode{ nullptr };
        PFN_vkCmdSetFrontFaceEXT SetFrontFace{ nullptr };
        PFN_vkCmdSetDepthBiasEnableEXT SetDepthBiasEnable{ nullptr };
        PFN_vkCmdSetRasterizationSamplesEXT SetRasterizationSamples{ nullptr };
        PFN_vkCmdSetSampleMaskEXT SetSampleMask{ nullptr };
        PFN_vkCmdSetAlphaToCoverageEnableEXT SetAlphaToCoverageEnable{ nullptr };
        PFN_vkCmdSetAlphaToOneEnableEXT SetAlphaToOneEnable{ nullptr };
        PFN_vkCmdSetColorBlendEnableEXT SetColorBlendEnable{ nullptr };
        PFN_vkCmdSetColorBlendEquationEXT SetColorBlendEquation{ nullptr };
        PFN_vkCmdSetColorWriteMaskEXT SetColorWriteMask{ nullptr };
        PFN_vkCmdSetLogicOpEnableEXT SetLogicOpEnable{ nullptr };
        PFN_vkCmdSetDepthTestEnableEXT SetDepthTestEnable{ nullptr };
        PFN_vkCmdSetDepthWriteEnableEXT SetDepthWriteEnable{ nullptr };
        PFN_vkCmdSetDepthCompareOpEXT SetDepthCompareOp{ nullptr };
        PFN_vkCmdSetDepthBoundsTestEnableEXT SetDepthBoundsTestEnable{ nullptr };
        PFN_vkCmdSetStencilTestEnableEXT SetStencilTestEnable{ nullptr };
    };

    struct Allocation
    {
        VkBuffer Buffer{ VK_NULL_HANDLE };
        VkDeviceMemory Memory{ VK_NULL_HANDLE };
    };

    struct Target
    {
        VkImage Image{ VK_NULL_HANDLE };
        VkImageView View{ VK_NULL_HANDLE };
        VkDeviceMemory Memory{ VK_NULL_HANDLE };
    };

    struct Bench
    {
        rhi::Device* Device{ nullptr };
        VkDevice VkDeviceHandle{ VK_NULL_HANDLE };
        VkPhysicalDevice PhysicalDevice{ VK_NULL_HANDLE };
        rhi::QueueHandle Queue{};
        uint32_t GraphicsFamily{ 0u };
        std::vector<uint32_t> ConcurrentFamilies;
        uint64_t TimestampMask{ 0u };
        double TimestampPeriodNs{ 0.0 };
        std::string DeviceName;
        ShaderObjectFunctions Dynamic{};
        ComputeBindings Bindings{};
        std::unique_ptr<rhi::ManifestProgram> ColorRaster;
        std::unique_ptr<rhi::ManifestProgram> DepthRaster;
        std::unique_ptr<rhi::ManifestProgram> Compute;
        Allocation Sums{};
        Allocation Readback{};
        std::unique_ptr<rhi::CommandPool> Pool;
        std::unique_ptr<rhi::QueryPool> Queries;
        std::unique_ptr<rhi::Fence> Fence;
    };

    struct CaseState
    {
        CaseSpec Spec{};
        Target Image{};
        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        VkDescriptorSet DescriptorSet{ VK_NULL_HANDLE };
        rhi::ManifestProgram* Raster{ nullptr };
        rhi::ImageBarrier ToAttachment{};
        rhi::ImageBarrier ToRead{};
    };

    [[nodiscard]] int32_t RunBenchmark(rhi::RhiSystem& rhi_system, const rhi::ShaderLibrary& library, bool validation);
    [[nodiscard]] bool ConfigureBench(rhi::Device& device, const rhi::ShaderLibrary& library, Bench& bench);
    [[nodiscard]] bool ReadTimestampInfo(Bench& bench);
    [[nodiscard]] std::optional<ShaderObjectFunctions> LoadShaderObjectFunctions(VkDevice vkDevice) noexcept;
    template<typename FunctionPointer>
    void LoadDeviceFunction(VkDevice vkDevice, const char* name, FunctionPointer& target, bool& allFound) noexcept;
    [[nodiscard]] std::optional<lodestone::VariantKey> FindVariant(const lodestone::ManifestIndex& index, std::span<const std::string_view> entryPoints);
    [[nodiscard]] std::optional<uint32_t> FindBinding(const lodestone::ManifestIndex& index, lodestone::VariantKey variant, std::string_view entryPoint, std::string_view resourceName);
    [[nodiscard]] std::optional<ComputeBindings> ReadComputeBindings(const lodestone::ManifestIndex& index);
    [[nodiscard]] std::unique_ptr<rhi::ManifestProgram> CreateProgram(rhi::Device& device, const rhi::ShaderLibrary& library, const lodestone::ManifestIndex& index, std::span<const std::string_view> entryPoints);
    [[nodiscard]] bool VkCallSucceeded(std::string_view call_name, VkResult result);
    [[nodiscard]] bool AllocateMemory(const Bench& bench, const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties, VkDeviceMemory& memory);
    [[nodiscard]] bool CreateBuffer(const Bench& bench, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, Allocation& allocation);
    [[nodiscard]] rhi::ImageAspect AspectOf(const CaseSpec& spec) noexcept;
    [[nodiscard]] std::string_view SharingName(const CaseSpec& spec) noexcept;
    [[nodiscard]] bool CreateTarget(const Bench& bench, const CaseSpec& spec, Target& target);
    void SetBarriers(const CaseSpec& spec, CaseState& state) noexcept;
    [[nodiscard]] bool CreateCaseDescriptors(const Bench& bench, CaseState& state);
    [[nodiscard]] bool PrepareCase(const Bench& bench, const CaseSpec& spec, CaseState& state);
    void DestroyCase(const Bench& bench, CaseState& state) noexcept;
    void RecordPasses(rhi::CommandBuffer& cmd, const Bench& bench, const CaseState& state);
    void SetRasterStates(const ShaderObjectFunctions& functions, VkCommandBuffer vk_cmd, bool depth_pass);
    [[nodiscard]] bool RunIteration(const Bench& bench, const CaseState& state, std::array<uint64_t, k_QueryCount>& stamps);
    [[nodiscard]] double TicksToMicroseconds(uint64_t start_tick, uint64_t end_tick, const Bench& bench) noexcept;
    [[nodiscard]] size_t NearestRankIndex(int32_t count, int32_t percent) noexcept;
    [[nodiscard]] PassStatistics Summarize(std::vector<double>& samples);
    [[nodiscard]] std::optional<CaseResult> RunCase(const CaseSpec& spec, Bench& bench);
    [[nodiscard]] std::optional<double> ReadChecksum(const Bench& bench);
    void PrintResults(const std::vector<CaseResult>& results);
    void TearDownBench(Bench& bench) noexcept;
}

int main(int argc, char** argv)
{
    bool validation = false;
    for (int32_t argIndex = 1; argIndex < argc; ++argIndex)
    {
        if (std::string_view{ argv[argIndex] } == "--validation")
        {
            validation = true;
        }
    }

    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "SharingBenchmark";
    createInfo.VkVersion = rhi::ApiVersion::Latest;
    createInfo.ValidationLevel = validation ? rhi::ValidationLayers::Full : rhi::ValidationLayers::None;
    createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
    createInfo.RequiredDeviceExtensions = { "VK_EXT_shader_object" };
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(SHARING_BENCHMARK_MANIFEST);
    if (!library.has_value())
    {
        std::cerr << std::format("SharingBenchmark: failed to open {}: {}\n", SHARING_BENCHMARK_MANIFEST, rhi::ToString(library.error()));
        return 1;
    }

    return RunBenchmark(*rhiSystem, **library, validation);
}

namespace
{
    int32_t RunBenchmark(rhi::RhiSystem& rhi_system, const rhi::ShaderLibrary& library, bool validation)
    {
        Bench bench{};
        if (!ConfigureBench(*rhi_system.GetDevice(), library, bench))
        {
            TearDownBench(bench);
            return 1;
        }

        std::println("SharingBenchmark: device {}, validation {}", bench.DeviceName, validation ? "on" : "off");
        std::println("graphics family {}, timestamp period {:.4f} ns, timestamp mask {:#x}, concurrent families {}",
            bench.GraphicsFamily, bench.TimestampPeriodNs, bench.TimestampMask, bench.ConcurrentFamilies.size());

        std::vector<CaseResult> results;
        results.reserve(k_Cases.size());
        for (const CaseSpec& spec : k_Cases)
        {
            std::optional<CaseResult> result = RunCase(spec, bench);
            if (!result.has_value())
            {
                TearDownBench(bench);
                return 1;
            }
            results.push_back(*result);
        }

        PrintResults(results);
        TearDownBench(bench);
        return 0;
    }

    bool ConfigureBench(rhi::Device& device, const rhi::ShaderLibrary& library, Bench& bench)
    {
        bench.Device = &device;
        bench.VkDeviceHandle = device.Handle().As<VkDevice>();
        bench.PhysicalDevice = device.GetPhysicalDevice().As<VkPhysicalDevice>();
        bench.Queue = device.GetGraphicsQueue(0u);

        const rhi::QueueFamilyIndices& families = device.GetQueueFamilyIndices();
        bench.GraphicsFamily = families.Graphics;
        for (const uint32_t family : { families.Graphics, families.Compute, families.Transfer })
        {
            if (family != ~0u && std::ranges::find(bench.ConcurrentFamilies, family) == bench.ConcurrentFamilies.end())
            {
                bench.ConcurrentFamilies.push_back(family);
            }
        }

        if (!ReadTimestampInfo(bench))
        {
            return false;
        }

        std::optional<ShaderObjectFunctions> functions = LoadShaderObjectFunctions(bench.VkDeviceHandle);
        if (!functions.has_value())
        {
            std::cerr << "SharingBenchmark: a shader object function is missing from the driver\n";
            return false;
        }
        bench.Dynamic = *functions;

        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(k_ModuleName);
        if (!index.has_value())
        {
            std::cerr << std::format("SharingBenchmark: module not found: {}\n", rhi::ToString(index.error()));
            return false;
        }
        const lodestone::ManifestIndex& moduleIndex = **index;

        bench.ColorRaster = CreateProgram(device, library, moduleIndex, k_ColorEntryPoints);
        bench.DepthRaster = CreateProgram(device, library, moduleIndex, k_DepthEntryPoints);
        bench.Compute = CreateProgram(device, library, moduleIndex, k_ComputeEntryPoints);
        if (!bench.ColorRaster || !bench.DepthRaster || !bench.Compute)
        {
            return false;
        }

        std::optional<ComputeBindings> bindings = ReadComputeBindings(moduleIndex);
        if (!bindings.has_value())
        {
            return false;
        }
        bench.Bindings = *bindings;

        const VkBufferUsageFlags sumsUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (!CreateBuffer(bench, k_SumBytes, sumsUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, bench.Sums))
        {
            return false;
        }

        const VkMemoryPropertyFlags hostMemory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!CreateBuffer(bench, k_SumBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, hostMemory, bench.Readback))
        {
            return false;
        }

        bench.Pool = std::make_unique<rhi::CommandPool>(device.Handle(), rhi::CommandPool::Type::Graphics, bench.GraphicsFamily);
        RhiAssert(bench.Pool->AllocateCommandBuffers(1u));
        bench.Queries = std::make_unique<rhi::QueryPool>(device.Handle(), rhi::QueryType::Timestamp, k_QueryCount);
        bench.Fence = std::make_unique<rhi::Fence>(device.Handle());
        return true;
    }

    bool ReadTimestampInfo(Bench& bench)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(bench.PhysicalDevice, &properties);
        bench.DeviceName = properties.deviceName;
        bench.TimestampPeriodNs = static_cast<double>(properties.limits.timestampPeriod);

        uint32_t familyCount = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(bench.PhysicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(bench.PhysicalDevice, &familyCount, families.data());

        const uint32_t validBits = families[bench.GraphicsFamily].timestampValidBits;
        if (validBits == 0u)
        {
            std::cerr << "SharingBenchmark: the graphics queue family has no timestamp bits\n";
            return false;
        }
        bench.TimestampMask = validBits >= 64u ? ~0ull : ((1ull << validBits) - 1ull);
        return true;
    }

    std::optional<ShaderObjectFunctions> LoadShaderObjectFunctions(VkDevice vkDevice) noexcept
    {
        ShaderObjectFunctions functions{};
        bool allFound = true;
        LoadDeviceFunction(vkDevice, "vkCmdSetPrimitiveTopologyEXT", functions.SetPrimitiveTopology, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetPrimitiveRestartEnableEXT", functions.SetPrimitiveRestartEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetVertexInputEXT", functions.SetVertexInput, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetDepthClampEnableEXT", functions.SetDepthClampEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetRasterizerDiscardEnableEXT", functions.SetRasterizerDiscardEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetPolygonModeEXT", functions.SetPolygonMode, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetCullModeEXT", functions.SetCullMode, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetFrontFaceEXT", functions.SetFrontFace, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetDepthBiasEnableEXT", functions.SetDepthBiasEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetRasterizationSamplesEXT", functions.SetRasterizationSamples, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetSampleMaskEXT", functions.SetSampleMask, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetAlphaToCoverageEnableEXT", functions.SetAlphaToCoverageEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetAlphaToOneEnableEXT", functions.SetAlphaToOneEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetColorBlendEnableEXT", functions.SetColorBlendEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetColorBlendEquationEXT", functions.SetColorBlendEquation, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetColorWriteMaskEXT", functions.SetColorWriteMask, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetLogicOpEnableEXT", functions.SetLogicOpEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetDepthTestEnableEXT", functions.SetDepthTestEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetDepthWriteEnableEXT", functions.SetDepthWriteEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetDepthCompareOpEXT", functions.SetDepthCompareOp, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetDepthBoundsTestEnableEXT", functions.SetDepthBoundsTestEnable, allFound);
        LoadDeviceFunction(vkDevice, "vkCmdSetStencilTestEnableEXT", functions.SetStencilTestEnable, allFound);

        if (!allFound)
        {
            return std::nullopt;
        }
        return functions;
    }

    template<typename FunctionPointer>
    void LoadDeviceFunction(VkDevice vkDevice, const char* name, FunctionPointer& target, bool& allFound) noexcept
    {
        target = reinterpret_cast<FunctionPointer>(vkGetDeviceProcAddr(vkDevice, name));
        allFound = allFound && target != nullptr;
    }

    std::optional<lodestone::VariantKey> FindVariant(const lodestone::ManifestIndex& index, std::span<const std::string_view> entryPoints)
    {
        std::expected<lodestone::VariantKey, lodestone::QueryError> variant = index.Query().SingleFor(entryPoints);
        if (!variant.has_value())
        {
            std::cerr << std::format("SharingBenchmark: variant query failed, code {}, axis '{}'\n",
                static_cast<int32_t>(variant.error().Code), variant.error().AxisName);
            return std::nullopt;
        }
        return *variant;
    }

    std::optional<uint32_t> FindBinding(const lodestone::ManifestIndex& index, lodestone::VariantKey variant, std::string_view entryPoint, std::string_view resourceName)
    {
        const lodestone::manifest::EnvironmentView& environment = index.View();
        lodestone::manifest::ManifestResult<uint32_t> entryPointIndex = environment.Module().FindEntryPointIndex(entryPoint);
        std::optional<lodestone::manifest::VariantView> variantView = environment.VariantByKey(variant);
        if (!entryPointIndex.has_value() || !variantView.has_value())
        {
            return std::nullopt;
        }

        const lodestone::manifest::EntryPointInstanceView instance = variantView->EntryPoint(*entryPointIndex);
        const lodestone::manifest::LayoutRange resources = instance.Layout();
        for (const lodestone::manifest::ResolvedResource resource : resources)
        {
            if (resource.Name() == resourceName)
            {
                return resource.PlacementValue().Word1;
            }
        }
        return std::nullopt;
    }

    std::optional<ComputeBindings> ReadComputeBindings(const lodestone::ManifestIndex& index)
    {
        std::optional<lodestone::VariantKey> variant = FindVariant(index, k_ComputeEntryPoints);
        if (!variant.has_value())
        {
            return std::nullopt;
        }

        std::optional<uint32_t> sourceBinding = FindBinding(index, *variant, k_ComputeEntryPoints[0], "Source");
        std::optional<uint32_t> sumsBinding = FindBinding(index, *variant, k_ComputeEntryPoints[0], "Sums");
        if (!sourceBinding.has_value() || !sumsBinding.has_value())
        {
            std::cerr << "SharingBenchmark: the manifest does not describe Source and Sums\n";
            return std::nullopt;
        }
        return ComputeBindings{ *sourceBinding, *sumsBinding };
    }

    std::unique_ptr<rhi::ManifestProgram> CreateProgram(rhi::Device& device, const rhi::ShaderLibrary& library, const lodestone::ManifestIndex& index, std::span<const std::string_view> entryPoints)
    {
        std::optional<lodestone::VariantKey> variant = FindVariant(index, entryPoints);
        if (!variant.has_value())
        {
            return nullptr;
        }

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program = library.CreateProgram(device.Handle(), k_ModuleName, *variant, entryPoints);
        if (!program.has_value())
        {
            std::cerr << std::format("SharingBenchmark: program creation failed: {}\n", rhi::ToString(program.error()));
            return nullptr;
        }
        return std::move(*program);
    }

    bool VkCallSucceeded(std::string_view call_name, VkResult result)
    {
        if (result != VK_SUCCESS)
        {
            std::cerr << std::format("SharingBenchmark: {} returned {}\n", call_name, static_cast<int32_t>(result));
            return false;
        }
        return true;
    }

    bool AllocateMemory(const Bench& bench, const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties, VkDeviceMemory& memory)
    {
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = bench.Device->GetMemoryTypeIndex(requirements.memoryTypeBits, properties);
        return VkCallSucceeded("vkAllocateMemory", vkAllocateMemory(bench.VkDeviceHandle, &allocateInfo, nullptr, &memory));
    }

    bool CreateBuffer(const Bench& bench, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, Allocation& allocation)
    {
        VkBufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        createInfo.size = size;
        createInfo.usage = usage;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!VkCallSucceeded("vkCreateBuffer", vkCreateBuffer(bench.VkDeviceHandle, &createInfo, nullptr, &allocation.Buffer)))
        {
            return false;
        }

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(bench.VkDeviceHandle, allocation.Buffer, &requirements);
        if (!AllocateMemory(bench, requirements, properties, allocation.Memory))
        {
            return false;
        }
        return VkCallSucceeded("vkBindBufferMemory", vkBindBufferMemory(bench.VkDeviceHandle, allocation.Buffer, allocation.Memory, 0u));
    }

    rhi::ImageAspect AspectOf(const CaseSpec& spec) noexcept
    {
        return spec.IsDepth ? rhi::ImageAspect::Depth : rhi::ImageAspect::Color;
    }

    std::string_view SharingName(const CaseSpec& spec) noexcept
    {
        return spec.Concurrent ? "concurrent" : "exclusive";
    }

    bool CreateTarget(const Bench& bench, const CaseSpec& spec, Target& target)
    {
        if (spec.Concurrent && bench.ConcurrentFamilies.size() < 2u)
        {
            std::cerr << "SharingBenchmark: concurrent sharing needs two or more queue families, this device has one\n";
            return false;
        }

        const VkImageUsageFlags attachmentUsage = spec.IsDepth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = spec.Format;
        imageInfo.extent = VkExtent3D{ k_Width, k_Height, 1u };
        imageInfo.mipLevels = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = attachmentUsage | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (spec.Concurrent)
        {
            imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            imageInfo.queueFamilyIndexCount = static_cast<uint32_t>(bench.ConcurrentFamilies.size());
            imageInfo.pQueueFamilyIndices = bench.ConcurrentFamilies.data();
        }
        else
        {
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        if (!VkCallSucceeded("vkCreateImage", vkCreateImage(bench.VkDeviceHandle, &imageInfo, nullptr, &target.Image)))
        {
            return false;
        }

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(bench.VkDeviceHandle, target.Image, &requirements);
        if (!AllocateMemory(bench, requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, target.Memory))
        {
            return false;
        }
        if (!VkCallSucceeded("vkBindImageMemory", vkBindImageMemory(bench.VkDeviceHandle, target.Image, target.Memory, 0u)))
        {
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = target.Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = spec.Format;
        viewInfo.subresourceRange = VkImageSubresourceRange{ static_cast<VkImageAspectFlags>(AspectOf(spec)), 0u, 1u, 0u, 1u };
        return VkCallSucceeded("vkCreateImageView", vkCreateImageView(bench.VkDeviceHandle, &viewInfo, nullptr, &target.View));
    }

    void SetBarriers(const CaseSpec& spec, CaseState& state) noexcept
    {
        const rhi::ImageHandle image{ reinterpret_cast<uint64_t>(state.Image.Image) };
        const rhi::ImageSubresourceRange range{ AspectOf(spec), 0u, 1u, 0u, 1u };

        if (spec.IsDepth)
        {
            state.ToAttachment = rhi::ImageBarrier{ image,
                rhi::PipelineStage::TopOfPipe, rhi::Access::None,
                rhi::PipelineStage::EarlyFragmentTests | rhi::PipelineStage::LateFragmentTests,
                rhi::Access::DepthStencilAttachmentWrite | rhi::Access::DepthStencilAttachmentRead,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::DepthStencilAttachment, range };
            state.ToRead = rhi::ImageBarrier{ image,
                rhi::PipelineStage::LateFragmentTests, rhi::Access::DepthStencilAttachmentWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::DepthStencilAttachment, rhi::ImageLayout::DepthStencilReadOnly, range };
        }
        else
        {
            state.ToAttachment = rhi::ImageBarrier{ image,
                rhi::PipelineStage::TopOfPipe, rhi::Access::None,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment, range };
            state.ToRead = rhi::ImageBarrier{ image,
                rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
                rhi::PipelineStage::ComputeShader, rhi::Access::ShaderSampledRead,
                rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::ShaderReadOnly, range };
        }
    }

    bool CreateCaseDescriptors(const Bench& bench, CaseState& state)
    {
        const std::array<VkDescriptorPoolSize, 2> poolSizes
        {
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1u },
            VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u },
        };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1u;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        if (!VkCallSucceeded("vkCreateDescriptorPool", vkCreateDescriptorPool(bench.VkDeviceHandle, &poolInfo, nullptr, &state.DescriptorPool)))
        {
            return false;
        }

        const std::span<const rhi::DescriptorSetLayoutHandle> setLayouts = bench.Compute->SetLayouts();
        if (setLayouts.empty())
        {
            std::cerr << "SharingBenchmark: the compute program has no descriptor set layout\n";
            return false;
        }
        const rhi::DescriptorSetLayoutHandle setLayoutHandle = setLayouts[0];
        const VkDescriptorSetLayout setLayout = setLayoutHandle.As<VkDescriptorSetLayout>();

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = state.DescriptorPool;
        allocateInfo.descriptorSetCount = 1u;
        allocateInfo.pSetLayouts = &setLayout;
        if (!VkCallSucceeded("vkAllocateDescriptorSets", vkAllocateDescriptorSets(bench.VkDeviceHandle, &allocateInfo, &state.DescriptorSet)))
        {
            return false;
        }

        VkDescriptorImageInfo imageDescriptor{};
        imageDescriptor.imageView = state.Image.View;
        imageDescriptor.imageLayout = state.Spec.IsDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorBufferInfo bufferDescriptor{};
        bufferDescriptor.buffer = bench.Sums.Buffer;
        bufferDescriptor.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = state.DescriptorSet;
        writes[0].dstBinding = bench.Bindings.SourceBinding;
        writes[0].descriptorCount = 1u;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[0].pImageInfo = &imageDescriptor;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = state.DescriptorSet;
        writes[1].dstBinding = bench.Bindings.SumsBinding;
        writes[1].descriptorCount = 1u;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].pBufferInfo = &bufferDescriptor;

        vkUpdateDescriptorSets(bench.VkDeviceHandle, static_cast<uint32_t>(writes.size()), writes.data(), 0u, nullptr);
        return true;
    }

    bool PrepareCase(const Bench& bench, const CaseSpec& spec, CaseState& state)
    {
        state.Spec = spec;
        state.Raster = spec.IsDepth ? bench.DepthRaster.get() : bench.ColorRaster.get();
        if (!CreateTarget(bench, spec, state.Image))
        {
            return false;
        }
        SetBarriers(spec, state);
        return CreateCaseDescriptors(bench, state);
    }

    void DestroyCase(const Bench& bench, CaseState& state) noexcept
    {
        vkDestroyDescriptorPool(bench.VkDeviceHandle, state.DescriptorPool, nullptr);
        vkDestroyImageView(bench.VkDeviceHandle, state.Image.View, nullptr);
        vkDestroyImage(bench.VkDeviceHandle, state.Image.Image, nullptr);
        vkFreeMemory(bench.VkDeviceHandle, state.Image.Memory, nullptr);
        state = CaseState{};
    }

    void RecordPasses(rhi::CommandBuffer& cmd, const Bench& bench, const CaseState& state)
    {
        const rhi::QueryPoolHandle queries = bench.Queries->Handle();
        const rhi::ImageViewHandle view{ reinterpret_cast<uint64_t>(state.Image.View) };
        const rhi::Rect2D renderArea{ 0, 0, k_Width, k_Height };
        const rhi::Viewport viewport{ 0.0f, 0.0f, static_cast<float>(k_Width), static_cast<float>(k_Height), 0.0f, 1.0f };
        const rhi::DependencyInfo toAttachment{ {}, {}, std::span{ &state.ToAttachment, 1 } };
        const rhi::DependencyInfo toRead{ {}, {}, std::span{ &state.ToRead, 1 } };

        cmd.Barrier(toAttachment);
        cmd.WriteTimestamp(rhi::PipelineStage::TopOfPipe, queries, 0u);

        rhi::ColorAttachment colorAttachment{};
        colorAttachment.ImageView = view;
        colorAttachment.ClearValue = rhi::ClearColorValue{ 0.0f, 0.0f, 0.0f, 0.0f };
        rhi::DepthStencilAttachment depthAttachment{};
        depthAttachment.ImageView = view;
        depthAttachment.DepthStoreOp = rhi::StoreOp::Store;
        rhi::RenderingInfo renderingInfo{};
        renderingInfo.RenderArea = renderArea;
        if (state.Spec.IsDepth)
        {
            renderingInfo.DepthStencilAttachment = depthAttachment;
        }
        else
        {
            renderingInfo.ColorAttachments = std::span{ &colorAttachment, 1 };
        }

        cmd.BeginRendering(renderingInfo);
        RhiAssert(cmd.BindShaders(state.Raster->Program()));
        SetRasterStates(bench.Dynamic, cmd.Handle().As<VkCommandBuffer>(), state.Spec.IsDepth);
        cmd.SetViewports(std::span{ &viewport, 1 });
        cmd.SetScissors(std::span{ &renderArea, 1 });
        cmd.Draw(3u, 1u, 0u, 0u);
        cmd.EndRendering();
        cmd.WriteTimestamp(rhi::PipelineStage::AllCommands, queries, 1u);

        cmd.Barrier(toRead);
        cmd.WriteTimestamp(rhi::PipelineStage::AllCommands, queries, 2u);

        RhiAssert(cmd.BindShaders(bench.Compute->Program()));
        const rhi::DescriptorSetHandle descriptorSet{ reinterpret_cast<uint64_t>(state.DescriptorSet) };
        cmd.BindDescriptorSets(rhi::PipelineBindPoint::Compute, bench.Compute->PipelineLayout(), 0u, std::span{ &descriptorSet, 1 });
        cmd.Dispatch(k_GroupsX, k_GroupsY, 1u);
        cmd.WriteTimestamp(rhi::PipelineStage::AllCommands, queries, 3u);
    }

    void SetRasterStates(const ShaderObjectFunctions& functions, VkCommandBuffer vk_cmd, bool depth_pass)
    {
        const VkBool32 depthEnable = depth_pass ? VK_TRUE : VK_FALSE;
        const VkBool32 blendEnables[1]{ VK_FALSE };
        const VkColorBlendEquationEXT blendEquations[1]
        {
            VkColorBlendEquationEXT{ VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD },
        };
        const VkColorComponentFlags writeMasks[1]
        {
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
        const VkSampleMask sampleMask = 0xffffffffu;

        functions.SetPrimitiveTopology(vk_cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        functions.SetPrimitiveRestartEnable(vk_cmd, VK_FALSE);
        functions.SetVertexInput(vk_cmd, 0u, nullptr, 0u, nullptr);
        functions.SetDepthClampEnable(vk_cmd, VK_FALSE);
        functions.SetRasterizerDiscardEnable(vk_cmd, VK_FALSE);
        functions.SetPolygonMode(vk_cmd, VK_POLYGON_MODE_FILL);
        functions.SetCullMode(vk_cmd, VK_CULL_MODE_NONE);
        functions.SetFrontFace(vk_cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        functions.SetDepthBiasEnable(vk_cmd, VK_FALSE);

        functions.SetRasterizationSamples(vk_cmd, VK_SAMPLE_COUNT_1_BIT);
        functions.SetSampleMask(vk_cmd, VK_SAMPLE_COUNT_1_BIT, &sampleMask);
        functions.SetAlphaToCoverageEnable(vk_cmd, VK_FALSE);
        functions.SetAlphaToOneEnable(vk_cmd, VK_FALSE);

        functions.SetColorBlendEnable(vk_cmd, 0u, 1u, blendEnables);
        functions.SetColorBlendEquation(vk_cmd, 0u, 1u, blendEquations);
        functions.SetColorWriteMask(vk_cmd, 0u, 1u, writeMasks);
        functions.SetLogicOpEnable(vk_cmd, VK_FALSE);

        functions.SetDepthTestEnable(vk_cmd, depthEnable);
        functions.SetDepthWriteEnable(vk_cmd, depthEnable);
        functions.SetDepthCompareOp(vk_cmd, VK_COMPARE_OP_ALWAYS);
        functions.SetDepthBoundsTestEnable(vk_cmd, VK_FALSE);
        functions.SetStencilTestEnable(vk_cmd, VK_FALSE);
    }

    bool RunIteration(const Bench& bench, const CaseState& state, std::array<uint64_t, k_QueryCount>& stamps)
    {
        bench.Fence->Reset();
        bench.Queries->Reset(0u, k_QueryCount);
        RhiAssert(bench.Pool->Reset());

        rhi::CommandBuffer cmd(bench.Pool->GetCommandBuffer(0u));
        RhiAssert(cmd.Begin());
        RecordPasses(cmd, bench, state);
        RhiAssert(cmd.End());

        const rhi::CommandBufferHandle handle = cmd.Handle();
        RhiAssert(rhi::QueueSubmit(bench.Queue, rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} }, bench.Fence->Handle()));

        if (!bench.Fence->Wait(k_TimeoutNs))
        {
            return false;
        }

        return bench.Queries->TryGetResults(0u, k_QueryCount, stamps);
    }

    double TicksToMicroseconds(uint64_t start_tick, uint64_t end_tick, const Bench& bench) noexcept
    {
        const uint64_t ticks = (end_tick - start_tick) & bench.TimestampMask;
        return static_cast<double>(ticks) * bench.TimestampPeriodNs / 1000.0;
    }

    size_t NearestRankIndex(int32_t count, int32_t percent) noexcept
    {
        const int32_t rank = (count * percent + 99) / 100;
        return static_cast<size_t>(rank - 1);
    }

    PassStatistics Summarize(std::vector<double>& samples)
    {
        std::ranges::sort(samples);
        const int32_t count = static_cast<int32_t>(std::ssize(samples));

        PassStatistics statistics{};
        statistics.MedianUs = samples[NearestRankIndex(count, 50)];
        statistics.P90Us = samples[NearestRankIndex(count, 90)];
        statistics.MeanUs = std::ranges::fold_left(samples, 0.0, std::plus<double>{}) / static_cast<double>(count);

        return statistics;
    }

    std::optional<CaseResult> RunCase(const CaseSpec& spec, Bench& bench)
    {
        CaseState state{};
        if (!PrepareCase(bench, spec, state))
        {
            DestroyCase(bench, state);
            return std::nullopt;
        }

        std::array<uint64_t, k_QueryCount> stamps{};
        std::vector<double> rasterUs;
        std::vector<double> computeUs;
        rasterUs.reserve(k_TimedIterations);
        computeUs.reserve(k_TimedIterations);
        for (int32_t iteration = 0; iteration < k_WarmupIterations + k_TimedIterations; ++iteration)
        {
            if (!RunIteration(bench, state, stamps))
            {
                std::cerr << std::format("SharingBenchmark: iteration {} of {} failed\n", iteration, spec.FormatName);
                DestroyCase(bench, state);
                return std::nullopt;
            }
            if (iteration < k_WarmupIterations)
            {
                continue;
            }

            rasterUs.push_back(TicksToMicroseconds(stamps[0], stamps[1], bench));
            computeUs.push_back(TicksToMicroseconds(stamps[2], stamps[3], bench));
        }

        CaseResult result{};
        result.Spec = spec;
        result.Raster = Summarize(rasterUs);
        result.Compute = Summarize(computeUs);
        const std::optional<double> checksum = ReadChecksum(bench);
        DestroyCase(bench, state);
        if (!checksum.has_value())
        {
            return std::nullopt;
        }
        result.Checksum = *checksum;
        return result;
    }

    std::optional<double> ReadChecksum(const Bench& bench)
    {
        bench.Fence->Reset();
        RhiAssert(bench.Pool->Reset());

        rhi::CommandBuffer cmd(bench.Pool->GetCommandBuffer(0u));
        RhiAssert(cmd.Begin());
            const rhi::BufferHandle sumsHandle{ reinterpret_cast<uint64_t>(bench.Sums.Buffer) };
            const rhi::BufferHandle readbackHandle{ reinterpret_cast<uint64_t>(bench.Readback.Buffer) };
            const rhi::GlobalBarrier toCopy{ rhi::PipelineStage::ComputeShader, rhi::Access::ShaderStorageWrite, rhi::PipelineStage::Copy, rhi::Access::TransferRead };
            cmd.Barrier(rhi::DependencyInfo{ std::span{ &toCopy, 1 }, {}, {} });
            const rhi::BufferCopy region{ 0u, 0u, k_SumBytes };
            cmd.CopyBuffer(sumsHandle, readbackHandle, std::span{ &region, 1 });
            const rhi::GlobalBarrier toHost{ rhi::PipelineStage::Copy, rhi::Access::TransferWrite, rhi::PipelineStage::Host, rhi::Access::HostRead };
            cmd.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1 }, {}, {} });
        RhiAssert(cmd.End());

        const rhi::CommandBufferHandle handle = cmd.Handle();
        RhiAssert(rhi::QueueSubmit(bench.Queue, rhi::SubmitInfo{ {}, std::span{ &handle, 1 }, {} }, bench.Fence->Handle()));
        if (!bench.Fence->Wait(k_TimeoutNs))
        {
            std::cerr << "SharingBenchmark: the checksum copy did not finish\n";
            return std::nullopt;
        }

        void* mapped = nullptr;
        if (!VkCallSucceeded("vkMapMemory", vkMapMemory(bench.VkDeviceHandle, bench.Readback.Memory, 0u, k_SumBytes, 0u, &mapped)))
        {
            return std::nullopt;
        }
        const std::span<const float> sums{ static_cast<const float*>(mapped), k_SumCount };
        const double checksum = std::ranges::fold_left(sums, 0.0, std::plus<double>{});
        vkUnmapMemory(bench.VkDeviceHandle, bench.Readback.Memory);
        return checksum;
    }

    void PrintResults(const std::vector<CaseResult>& results)
    {
        std::println("");
        std::println("{:<22} {:<11} {:>14} {:>14} {:>14} {:>14} {:>14} {:>14} {:>14}",
            "format", "sharing", "raster med us", "raster p90 us", "raster mean us", "compute med us", "compute p90 us", "compute mean us", "checksum");

        for (const CaseResult& result : results)
        {
            std::println("{:<22} {:<11} {:>14.2f} {:>14.2f} {:>14.3f} {:>14.2f} {:>14.2f} {:>14.3f} {:>14.6e}",
                result.Spec.FormatName, SharingName(result.Spec),
                result.Raster.MedianUs, result.Raster.P90Us, result.Raster.MeanUs,
                result.Compute.MedianUs, result.Compute.P90Us, result.Compute.MeanUs, result.Checksum);
        }

        std::println("");

        for (const CaseResult& result : results)
        {
            std::println("{{\"format\":\"{}\",\"sharing\":\"{}\",\"raster_median_us\":{:.3f},\"raster_p90_us\":{:.3f},\"raster_mean_us\":{:.3f},\"compute_median_us\":{:.3f},\"compute_p90_us\":{:.3f},\"compute_mean_us\":{:.3f},\"checksum\":{:.6e}}}",
                result.Spec.FormatName, SharingName(result.Spec),
                result.Raster.MedianUs, result.Raster.P90Us, result.Raster.MeanUs,
                result.Compute.MedianUs, result.Compute.P90Us, result.Compute.MeanUs, result.Checksum);
        }
    }

    void TearDownBench(Bench& bench) noexcept
    {
        if (bench.VkDeviceHandle == VK_NULL_HANDLE)
        {
            return;
        }
        vkDestroyBuffer(bench.VkDeviceHandle, bench.Sums.Buffer, nullptr);
        vkFreeMemory(bench.VkDeviceHandle, bench.Sums.Memory, nullptr);
        vkDestroyBuffer(bench.VkDeviceHandle, bench.Readback.Buffer, nullptr);
        vkFreeMemory(bench.VkDeviceHandle, bench.Readback.Memory, nullptr);
        bench.Sums = Allocation{};
        bench.Readback = Allocation{};
    }
}
