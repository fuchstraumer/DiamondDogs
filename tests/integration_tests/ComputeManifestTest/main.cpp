#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "ResourceBarriers.hpp"
#include "ResourceContext.hpp"
#include "RhiAssert.hpp"
#include "RhiSystem.hpp"
#include "ShaderLibrary.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestIndex.hpp"
#include "ShaderProgram.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <chrono>
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
#include <thread>
#include <vector>

// Cooks HashFill.slang through Lodestone (a build step), opens the bundle with rhi::ShaderLibrary, and runs
// both variants of its HASH_TWICE axis headless. Every output word is compared with the same hash on the CPU.
// The uniform member offsets and the binding numbers are read from the manifest, never written down here,
// so the test also proves that the CPU and the shader agree on the layout.
//
// Every buffer comes from ResourceContext, one per path through it: Params is host-visible and written directly,
// Output is device-local and its sentinel fill goes through the transfer system (the dispatch waits on the reply's
// timeline semaphore), and Readback is a GPUToCPU buffer read through MapBuffer.

namespace
{
    // not a multiple of the workgroup size, so the bounds check in the shader is exercised too
    constexpr uint32_t k_ElementCount = 4099u;
    constexpr uint32_t k_Seed = 0x9E3779B9u;
    constexpr uint32_t k_Sentinel = 0xDEADBEEFu;
    constexpr std::string_view k_ModuleName{ "HashFill" };
    constexpr std::array<std::string_view, 1> k_EntryPoints{ "HashFillCS" };

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
    [[nodiscard]] bool WaitUntilResourceIsKnown(const GraphicsResourceReply& reply) noexcept;
    [[nodiscard]] int32_t RunVariant(rhi::RhiSystem& rhi_system, ResourceContext& resources, const rhi::ShaderLibrary& library, bool hash_twice);

    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;
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
    {
        ResourceContext resources;
        resources.Initialize(ResourceContextCreateInfo{ rhiSystem->GetInstance(), rhiSystem->GetDevice(), rhiSystem->GetVulkanApiVersion(), true });
        for (const bool hashTwice : { false, true })
        {
            failures += RunVariant(*rhiSystem, resources, **library, hashTwice);
        }
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

    bool WaitUntilResourceIsKnown(const GraphicsResourceReply& reply) noexcept
    {
        // the handles are known once the reply reaches Transferring. the dispatch waits for the upload on the GPU,
        // through the reply's semaphore, so the CPU does not wait for the transfer itself
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(k_TimeoutNs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const MessageReply::Status status = reply.GetStatus();
            if (status == MessageReply::Status::Transferring || status == MessageReply::Status::Completed)
            {
                return true;
            }
            if (status == MessageReply::Status::Failed)
            {
                return false;
            }
            std::this_thread::yield();
        }
        return false;
    }
    int32_t RunVariant(rhi::RhiSystem& rhi_system, ResourceContext& resources, const rhi::ShaderLibrary& library, bool hash_twice)
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

        const size_t paramsBytes = static_cast<size_t>(std::max<uint64_t>(paramsLayout->ParamsByteSize, 16u));
        std::vector<std::byte> paramsData(paramsBytes);
        std::memcpy(paramsData.data() + paramsLayout->SeedOffset, &k_Seed, sizeof(k_Seed));
        std::memcpy(paramsData.data() + paramsLayout->CountOffset, &k_ElementCount, sizeof(k_ElementCount));
        const RhiBufferResourceData paramsInitial{ std::span<const std::byte>{ paramsData }, 0u };
        BufferCreateInfo paramsInfo{};
        paramsInfo.Domain = ResourceDomain::CPUToGPU;
        paramsInfo.Usage = BufferUsageBits::ComputeShaderUBO;
        paramsInfo.Size = paramsBytes;
        std::shared_ptr<GraphicsResourceReply> paramsReply = resources.CreateBuffer(paramsInfo, std::span{ &paramsInitial, 1u });

        const size_t outputBytes = sizeof(uint32_t) * k_ElementCount;
        const std::vector<uint32_t> sentinel(k_ElementCount, k_Sentinel);
        const RhiBufferResourceData outputInitial{ std::as_bytes(std::span{ sentinel }), 0u };
        BufferCreateInfo outputInfo{};
        outputInfo.Domain = ResourceDomain::GPUOnly;
        // the dispatch result is copied to the readback buffer. the sentinel upload adds CopyDestination itself
        outputInfo.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource;
        outputInfo.Size = outputBytes;
        std::shared_ptr<GraphicsResourceReply> outputReply = resources.CreateBuffer(outputInfo, std::span{ &outputInitial, 1u });

        BufferCreateInfo readbackInfo{};
        readbackInfo.Domain = ResourceDomain::GPUToCPU;
        readbackInfo.Usage = BufferUsageBits::CopyDestination;
        readbackInfo.Size = outputBytes;
        std::shared_ptr<GraphicsResourceReply> readbackReply = resources.CreateBuffer(readbackInfo, {});

        if (paramsReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed ||
            readbackReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed ||
            !WaitUntilResourceIsKnown(*outputReply))
        {
            std::cerr << std::format("[{}] ResourceContext did not create the buffers\n", variantName);
            return 1;
        }
        const GraphicsResource params = paramsReply->GetResource();
        const GraphicsResource output = outputReply->GetResource();
        const GraphicsResource readback = readbackReply->GetResource();

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

        const VkDescriptorBufferInfo paramsDescriptor{ reinterpret_cast<VkBuffer>(params.VkHandle), 0, VK_WHOLE_SIZE };
        const VkDescriptorBufferInfo outputDescriptor{ reinterpret_cast<VkBuffer>(output.VkHandle), 0, VK_WHOLE_SIZE };
        const VkWriteDescriptorSet writes[2]
        {
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, paramsLayout->ParamsBinding, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &paramsDescriptor, nullptr },
            { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, paramsLayout->OutputBinding, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &outputDescriptor, nullptr },
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

            const BufferBarrierInfo toCopy{ rhi::BufferHandle{ output.VkHandle }, BufferUsageBits::ComputeShaderUAV, BufferUsageBits::CopySource };
            ExecuteBufferBarrier(commandBuffer.Handle(), toCopy);
            const VkBufferCopy copyRegion{ 0u, 0u, outputBytes };
            vkCmdCopyBuffer(vkCommandBuffer, reinterpret_cast<VkBuffer>(output.VkHandle), reinterpret_cast<VkBuffer>(readback.VkHandle), 1, &copyRegion);

            const VkMemoryBarrier2 toHost
            {
                VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                nullptr,
                VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_HOST_BIT,
                VK_ACCESS_2_HOST_READ_BIT
            };
            const VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 1, &toHost, 0, nullptr, 0, nullptr };
            vkCmdPipelineBarrier2(vkCommandBuffer, &dependency);
            RhiAssert(commandBuffer.End());

            // the sentinel upload runs on the transfer queue, and signals the output reply's semaphore when it finishes
            const VkSemaphore uploadSemaphore = reinterpret_cast<VkSemaphore>(outputReply->SemaphoreHandle());
            const uint64_t uploadValue = k_TransferCompleteSemaphoreValue;
            const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            const VkTimelineSemaphoreSubmitInfo timelineInfo{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr, 1, &uploadValue, 0, nullptr };
            rhi::Fence fence(device.Handle());
            const VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO, &timelineInfo, 1, &uploadSemaphore, &waitStage, 1, &vkCommandBuffer, 0, nullptr };
            RhiAssert(rhi::Result(vkQueueSubmit(device.GetGeneralQueue().As<VkQueue>(), 1, &submitInfo, fence.Handle().As<VkFence>())));
            if (!fence.Wait())
            {
                std::cerr << std::format("[{}] the dispatch did not finish\n", variantName);
                return 1;
            }
        }

        std::shared_ptr<PointerMessageReply> mapReply = resources.MapBuffer(readback, outputBytes, 0u);
        if (mapReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            std::cerr << std::format("[{}] MapBuffer failed on the readback buffer\n", variantName);
            return 1;
        }
        const std::span<const uint32_t> outputWords{ static_cast<const uint32_t*>(mapReply->GetPointer()), k_ElementCount };

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

        std::shared_ptr<MessageReply> unmapReply = resources.UnmapBuffer(readback, outputBytes, 0u);
        if (unmapReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
        {
            std::cerr << std::format("[{}] UnmapBuffer failed\n", variantName);
            ++mismatches;
        }

        vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
        for (const GraphicsResource& resource : { params, output, readback })
        {
            std::shared_ptr<MessageReply> destroyReply = resources.DestroyResource(resource);
            if (destroyReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
            {
                std::cerr << std::format("[{}] DestroyResource failed\n", variantName);
                ++mismatches;
            }
        }
        if (mismatches != 0u)
        {
            std::cerr << std::format("[{}] {} of {} words differ from the CPU reference\n", variantName, mismatches, k_ElementCount);
            return 1;
        }
        std::cout << std::format("[{}] {} words match the CPU reference\n", variantName, k_ElementCount);
        return 0;
    }
}
