#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
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
#include <cstring>
#include <expected>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Cooks HashFill.slang through Lodestone (a build step), opens the bundle with rhi::ShaderLibrary, and runs
// both variants of its HASH_TWICE axis headless. Every output word is compared with the same hash on the CPU.
// The uniform member offsets and the binding numbers are read from the manifest, never written down here,
// so the test also proves that the CPU and the shader agree on the layout.

namespace
{
    // not a multiple of the workgroup size, so the bounds check in the shader is exercised too
    constexpr uint32_t k_ElementCount = 4099u;
    constexpr uint32_t k_Seed = 0x9E3779B9u;
    constexpr uint32_t k_Sentinel = 0xDEADBEEFu;
    constexpr std::string_view k_ModuleName{ "HashFill" };
    constexpr std::array<std::string_view, 1> k_EntryPoints{ "HashFillCS" };

    struct HostVisibleBuffer
    {
        VkBuffer Buffer{ VK_NULL_HANDLE };
        VkDeviceMemory Memory{ VK_NULL_HANDLE };
        void* Mapped{ nullptr };
        VkDeviceSize Size{ 0u };
    };

    struct ParamsLayout
    {
        uint32_t ParamsBinding{ 0u };
        uint32_t OutputBinding{ 0u };
        uint32_t SeedOffset{ 0u };
        uint32_t CountOffset{ 0u };
        uint64_t ParamsByteSize{ 0u };
    };

    [[nodiscard]] uint32_t PcgHash(uint32_t value) noexcept;
    [[nodiscard]] uint32_t ExpectedWord(uint32_t index, bool hash_twice) noexcept;
    [[nodiscard]] std::optional<ParamsLayout> ReadParamsLayout(const lodestone::ManifestIndex& index, lodestone::VariantKey variant);
    [[nodiscard]] HostVisibleBuffer CreateHostVisibleBuffer(const rhi::Device& device, VkDeviceSize size, VkBufferUsageFlags usage);
    void DestroyBuffer(VkDevice device, HostVisibleBuffer& buffer) noexcept;
    [[nodiscard]] int32_t RunVariant(rhi::RhiSystem& rhi_system, const rhi::ShaderLibrary& library, bool hash_twice);
}

int main()
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "ComputeManifestTest";
    createInfo.VkVersion = rhi::ApiVersion::Latest;
    createInfo.ValidationLevel = rhi::ValidationLayers::Full;
    createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
    createInfo.RequiredDeviceExtensions = { "VK_EXT_shader_object" };
    auto rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);

    rhi::ShaderLibraryResult<std::unique_ptr<rhi::ShaderLibrary>> library = rhi::ShaderLibrary::Open(COMPUTE_MANIFEST_TEST_MANIFEST);
    if (!library.has_value())
    {
        std::cerr << std::format("Failed to open {}: {}\n", COMPUTE_MANIFEST_TEST_MANIFEST, rhi::ToString(library.error()));
        return 1;
    }

    int32_t failures = 0;
    for (const bool hashTwice : { false, true })
    {
        failures += RunVariant(*rhiSystem, **library, hashTwice);
    }

    if (failures != 0)
    {
        std::cerr << std::format("ComputeManifestTest: {} failure(s)\n", failures);
        return 1;
    }
    std::cout << "ComputeManifestTest: both variants match the CPU reference\n";
    return 0;
}

namespace
{
    uint32_t PcgHash(uint32_t value) noexcept
    {
        const uint32_t state = value * 747796405u + 2891336453u;
        const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    uint32_t ExpectedWord(uint32_t index, bool hash_twice) noexcept
    {
        const uint32_t hashed = PcgHash(index ^ k_Seed);
        return hash_twice ? PcgHash(hashed) : hashed;
    }

    std::optional<ParamsLayout> ReadParamsLayout(const lodestone::ManifestIndex& index, lodestone::VariantKey variant)
    {
        const lodestone::manifest::EnvironmentView& environment = index.View();
        lodestone::manifest::ManifestResult<uint32_t> entryPointIndex = environment.Module().FindEntryPointIndex(k_EntryPoints[0]);
        std::optional<lodestone::manifest::VariantView> variantView = environment.VariantByKey(variant);
        if (!entryPointIndex.has_value() || !variantView.has_value())
        {
            return std::nullopt;
        }

        ParamsLayout layout{};
        bool foundParams = false;
        bool foundOutput = false;
        bool foundSeed = false;
        bool foundCount = false;
        const lodestone::manifest::EntryPointInstanceView instance = variantView->EntryPoint(*entryPointIndex);
        const lodestone::manifest::LayoutRange resources = instance.Layout();
        for (const lodestone::manifest::ResolvedResource resource : resources)
        {
            if (resource.Name() == "Output")
            {
                layout.OutputBinding = resource.PlacementValue().Word1;
                foundOutput = true;
            }
            if (resource.Name() != "Params")
            {
                continue;
            }
            layout.ParamsBinding = resource.PlacementValue().Word1;
            layout.ParamsByteSize = resource.ByteSize();
            foundParams = true;
            for (const lodestone::manifest::UniformMemberView member : resource.Members())
            {
                if (member.Name().ends_with("Seed"))
                {
                    layout.SeedOffset = member.Offset();
                    foundSeed = true;
                }
                if (member.Name().ends_with("Count"))
                {
                    layout.CountOffset = member.Offset();
                    foundCount = true;
                }
            }
        }

        if (!(foundParams && foundOutput && foundSeed && foundCount))
        {
            return std::nullopt;
        }
        return layout;
    }

    HostVisibleBuffer CreateHostVisibleBuffer(const rhi::Device& device, VkDeviceSize size, VkBufferUsageFlags usage)
    {
        const VkDevice vkDevice = device.Handle().As<VkDevice>();
        HostVisibleBuffer result{};
        result.Size = size;

        const VkBufferCreateInfo bufferInfo
        {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            nullptr,
            0,
            size,
            usage,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr
        };
        RhiAssert(rhi::Result(vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &result.Buffer)));

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(vkDevice, result.Buffer, &requirements);
        const uint32_t memoryType = device.GetMemoryTypeIndex(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        const VkMemoryAllocateInfo allocateInfo
        {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            requirements.size,
            memoryType
        };
        RhiAssert(rhi::Result(vkAllocateMemory(vkDevice, &allocateInfo, nullptr, &result.Memory)));
        RhiAssert(rhi::Result(vkBindBufferMemory(vkDevice, result.Buffer, result.Memory, 0)));
        RhiAssert(rhi::Result(vkMapMemory(vkDevice, result.Memory, 0, VK_WHOLE_SIZE, 0, &result.Mapped)));
        return result;
    }

    void DestroyBuffer(VkDevice device, HostVisibleBuffer& buffer) noexcept
    {
        if (buffer.Mapped != nullptr)
        {
            vkUnmapMemory(device, buffer.Memory);
        }
        vkDestroyBuffer(device, buffer.Buffer, nullptr);
        vkFreeMemory(device, buffer.Memory, nullptr);
        buffer = HostVisibleBuffer{};
    }

    int32_t RunVariant(rhi::RhiSystem& rhi_system, const rhi::ShaderLibrary& library, bool hash_twice)
    {
        const std::string variantName = std::format("HASH_TWICE={}", hash_twice);
        rhi::Device& device = *rhi_system.GetDevice();
        const VkDevice vkDevice = device.Handle().As<VkDevice>();

        rhi::ShaderLibraryResult<const lodestone::ManifestIndex*> index = library.ModuleIndex(k_ModuleName);
        if (!index.has_value())
        {
            std::cerr << std::format("[{}] module {} not found: {}\n", variantName, k_ModuleName, rhi::ToString(index.error()));
            return 1;
        }

        std::expected<lodestone::VariantKey, lodestone::QueryError> variant =
            (*index)->Query().Where("HASH_TWICE", hash_twice).SingleFor(k_EntryPoints);
        if (!variant.has_value())
        {
            std::cerr << std::format("[{}] variant query failed, code {}, axis '{}'\n",
                variantName, static_cast<int>(variant.error().Code), variant.error().AxisName);
            return 1;
        }

        std::optional<ParamsLayout> paramsLayout = ReadParamsLayout(**index, *variant);
        if (!paramsLayout.has_value())
        {
            std::cerr << std::format("[{}] the manifest does not describe Params and Output as expected\n", variantName);
            return 1;
        }

        rhi::ShaderLibraryResult<std::unique_ptr<rhi::ManifestProgram>> program =
            library.CreateProgram(device.Handle(), k_ModuleName, *variant, k_EntryPoints);
        if (!program.has_value())
        {
            std::cerr << std::format("[{}] program creation failed: {}\n", variantName, rhi::ToString(program.error()));
            return 1;
        }

        HostVisibleBuffer params = CreateHostVisibleBuffer(device, std::max<VkDeviceSize>(paramsLayout->ParamsByteSize, 16u), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        HostVisibleBuffer output = CreateHostVisibleBuffer(device, sizeof(uint32_t) * k_ElementCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::byte* paramsBytes = static_cast<std::byte*>(params.Mapped);
        std::memcpy(paramsBytes + paramsLayout->SeedOffset, &k_Seed, sizeof(k_Seed));
        std::memcpy(paramsBytes + paramsLayout->CountOffset, &k_ElementCount, sizeof(k_ElementCount));
        std::span<uint32_t> outputWords{ static_cast<uint32_t*>(output.Mapped), k_ElementCount };
        std::ranges::fill(outputWords, k_Sentinel);

        constexpr VkDescriptorPoolSize poolSizes[2]
        {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 },
        };
        const VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 2, poolSizes };
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        RhiAssert(rhi::Result(vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool)));

        std::span<const rhi::DescriptorSetLayoutHandle> setLayouts = (*program)->SetLayouts();
        const VkDescriptorSetLayout setLayout = setLayouts[0].As<VkDescriptorSetLayout>();
        const VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &setLayout };
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        RhiAssert(rhi::Result(vkAllocateDescriptorSets(vkDevice, &setInfo, &descriptorSet)));

        const VkDescriptorBufferInfo paramsInfo{ params.Buffer, 0, VK_WHOLE_SIZE };
        const VkDescriptorBufferInfo outputInfo{ output.Buffer, 0, VK_WHOLE_SIZE };
        const VkWriteDescriptorSet writes[2]
        {
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, paramsLayout->ParamsBinding, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &paramsInfo, nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, paramsLayout->OutputBinding, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputInfo, nullptr },
        };
        vkUpdateDescriptorSets(vkDevice, 2, writes, 0, nullptr);

        {
            rhi::CommandPool commandPool(device.Handle(), rhi::CommandPool::Type::Compute, device.GetQueueFamilyIndices().Graphics);
            RhiAssert(commandPool.AllocateCommandBuffers(1));
            rhi::CommandBuffer commandBuffer(commandPool.GetCommandBuffer(0));
            const VkCommandBuffer vkCommandBuffer = commandBuffer.Handle().As<VkCommandBuffer>();

            RhiAssert(commandBuffer.Begin());
            RhiAssert((*program)->Program().BindShaders(commandBuffer.Handle()));
            vkCmdBindDescriptorSets(vkCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                (*program)->PipelineLayout().As<VkPipelineLayout>(), 0, 1, &descriptorSet, 0, nullptr);
            const uint32_t workgroupX = (*program)->Workgroup()[0];
            vkCmdDispatch(vkCommandBuffer, (k_ElementCount + workgroupX - 1u) / workgroupX, 1, 1);

            const VkMemoryBarrier2 toHost
            {
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                nullptr,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_HOST_BIT,
                VK_ACCESS_2_HOST_READ_BIT
            };
            const VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &toHost, 0, nullptr, 0, nullptr };
            vkCmdPipelineBarrier2(vkCommandBuffer, &dependency);
            RhiAssert(commandBuffer.End());

            rhi::Fence fence(device.Handle());
            const VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &vkCommandBuffer, 0, nullptr };
            RhiAssert(rhi::Result(vkQueueSubmit(device.GetGeneralQueue().As<VkQueue>(), 1, &submitInfo, fence.Handle().As<VkFence>())));
            if (!fence.Wait())
            {
                std::cerr << std::format("[{}] the dispatch did not finish\n", variantName);
                return 1;
            }
        }

        uint32_t mismatches = 0u;
        for (uint32_t i = 0; i < k_ElementCount; ++i)
        {
            const uint32_t expected = ExpectedWord(i, hash_twice);
            if (outputWords[i] != expected)
            {
                if (mismatches < 8u)
                {
                    std::cerr << std::format("[{}] word {}: expected 0x{:08X}, got 0x{:08X}\n", variantName, i, expected, outputWords[i]);
                }
                ++mismatches;
            }
        }

        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        DestroyBuffer(vkDevice, params);
        DestroyBuffer(vkDevice, output);

        if (mismatches != 0u)
        {
            std::cerr << std::format("[{}] {} of {} words differ from the CPU reference\n", variantName, mismatches, k_ElementCount);
            return 1;
        }
        std::cout << std::format("[{}] {} words match the CPU reference\n", variantName, k_ElementCount);
        return 0;
    }
}
