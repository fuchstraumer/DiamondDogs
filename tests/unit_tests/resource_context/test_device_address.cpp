#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFuture.hpp"
#include "ResourcePlacement.hpp"
#include "ResourceTypes.hpp"
#include "RhiFlags.hpp"
#include "RhiResourceFlags.hpp"
#include "RhiTypes.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{
    constexpr std::string_view k_DeviceAddressFeature{ "VkPhysicalDeviceVulkan12Features.bufferDeviceAddress" };
    constexpr uint32_t k_WordCount = 1024u;
    constexpr uint32_t k_GroupSize = 64u;
    constexpr size_t k_BufferBytes = size_t{ k_WordCount } * sizeof(uint32_t);

    // write_pattern.comp (glslc -O, Vulkan 1.3 target). The cooked path needs a manifest and a bundle, and this test
    // needs one pipeline with one push constant, so the module is kept as words, as in test_barriers.cpp
    //
    //   #version 450
    //   #extension GL_EXT_buffer_reference : require
    //   layout(local_size_x = 64) in;
    //   layout(buffer_reference, std430) buffer Target { uint words[]; };
    //   layout(push_constant) uniform Push { Target target; uint seed; } push;
    //   void main()
    //   {
    //       uint index = gl_GlobalInvocationID.x;
    //       push.target.words[index] = (index * 2654435761u) ^ push.seed;
    //   }
    constexpr uint32_t k_WritePatternSpirv[] =
    {
        0x07230203, 0x00010600, 0x000d000b, 0x00000026, 0x00000000, 0x00020011,
        0x00000001, 0x00020011, 0x000014e3, 0x0006000b, 0x00000001, 0x4c534c47,
        0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x000014e4, 0x00000001,
        0x0007000f, 0x00000005, 0x00000004, 0x6e69616d, 0x00000000, 0x0000000b,
        0x00000015, 0x00060010, 0x00000004, 0x00000011, 0x00000040, 0x00000001,
        0x00000001, 0x00040047, 0x0000000b, 0x0000000b, 0x0000001c, 0x00030047,
        0x00000011, 0x00000002, 0x00050048, 0x00000011, 0x00000000, 0x00000023,
        0x00000000, 0x00050048, 0x00000011, 0x00000001, 0x00000023, 0x00000008,
        0x00040047, 0x00000012, 0x00000006, 0x00000004, 0x00030047, 0x00000013,
        0x00000002, 0x00050048, 0x00000013, 0x00000000, 0x00000023, 0x00000000,
        0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015,
        0x00000006, 0x00000020, 0x00000000, 0x00040017, 0x00000009, 0x00000006,
        0x00000003, 0x00040020, 0x0000000a, 0x00000001, 0x00000009, 0x0004003b,
        0x0000000a, 0x0000000b, 0x00000001, 0x0004002b, 0x00000006, 0x0000000c,
        0x00000000, 0x00040020, 0x0000000d, 0x00000001, 0x00000006, 0x00030027,
        0x00000010, 0x000014e5, 0x0004001e, 0x00000011, 0x00000010, 0x00000006,
        0x0003001d, 0x00000012, 0x00000006, 0x0003001e, 0x00000013, 0x00000012,
        0x00040020, 0x00000010, 0x000014e5, 0x00000013, 0x00040020, 0x00000014,
        0x00000009, 0x00000011, 0x0004003b, 0x00000014, 0x00000015, 0x00000009,
        0x00040015, 0x00000016, 0x00000020, 0x00000001, 0x0004002b, 0x00000016,
        0x00000017, 0x00000000, 0x00040020, 0x00000018, 0x00000009, 0x00000010,
        0x0004002b, 0x00000006, 0x0000001d, 0x9e3779b1, 0x0004002b, 0x00000016,
        0x0000001f, 0x00000001, 0x00040020, 0x00000020, 0x00000009, 0x00000006,
        0x00040020, 0x00000024, 0x000014e5, 0x00000006, 0x00050036, 0x00000002,
        0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041,
        0x0000000d, 0x0000000e, 0x0000000b, 0x0000000c, 0x0004003d, 0x00000006,
        0x0000000f, 0x0000000e, 0x00050041, 0x00000018, 0x00000019, 0x00000015,
        0x00000017, 0x0004003d, 0x00000010, 0x0000001a, 0x00000019, 0x00050084,
        0x00000006, 0x0000001e, 0x0000000f, 0x0000001d, 0x00050041, 0x00000020,
        0x00000021, 0x00000015, 0x0000001f, 0x0004003d, 0x00000006, 0x00000022,
        0x00000021, 0x000500c6, 0x00000006, 0x00000023, 0x0000001e, 0x00000022,
        0x00060041, 0x00000024, 0x00000025, 0x0000001a, 0x00000017, 0x0000000f,
        0x0005003e, 0x00000025, 0x00000023, 0x00000002, 0x00000004, 0x000100fd,
        0x00010038,
    };

    struct PatternPush
    {
        uint64_t Address{ 0u };
        uint32_t Seed{ 0u };
        uint32_t Padding{ 0u };
    };

    static_assert(sizeof(PatternPush) == 16u);

    [[nodiscard]] BufferCreateInfo AddressBuffer(size_t size);
}

// A context with device addresses on, and one compute pipeline that writes a known pattern through a 64-bit address
// that it gets as a push constant. Every test skips when the device does not enable bufferDeviceAddress.
class DeviceAddressTest : public ResourceContextTest
{
protected:
    void SetUp() override
    {
        ResourceContextTest::SetUp();
        ASSERT_NE(rhiSystem, nullptr);

        device = rhiSystem->GetDevice();
        ASSERT_NE(device, nullptr);

        if (!device->IsFeatureEnabled(k_DeviceAddressFeature))
        {
            GTEST_SKIP() << "the test device does not enable " << k_DeviceAddressFeature;
        }

        ASSERT_TRUE(recreateContextWithDeviceAddresses());

        vkDevice = device->Handle().As<VkDevice>();
        queue = device->GetGeneralQueue();
        commandPool = std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Graphics, device->GetQueueFamilyIndices().Graphics);
        ASSERT_TRUE(commandPool->AllocateCommandBuffers(1).IsSuccess());

        const VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, sizeof(k_WritePatternSpirv), k_WritePatternSpirv };
        ASSERT_EQ(vkCreateShaderModule(vkDevice, &moduleInfo, nullptr, &shaderModule), VK_SUCCESS);

        const VkPushConstantRange pushRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(PatternPush) };
        const VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 0, nullptr, 1, &pushRange };
        ASSERT_EQ(vkCreatePipelineLayout(vkDevice, &layoutInfo, nullptr, &pipelineLayout), VK_SUCCESS);

        const VkComputePipelineCreateInfo pipelineInfo
        {
            VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0,
            VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shaderModule, "main", nullptr },
            pipelineLayout, VK_NULL_HANDLE, 0
        };
        ASSERT_EQ(vkCreateComputePipelines(vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), VK_SUCCESS);
    }

    void TearDown() override
    {
        if (device != nullptr && vkDevice != VK_NULL_HANDLE)
        {
            device->WaitDeviceIdle();
            vkDestroyPipeline(vkDevice, pipeline, nullptr);
            vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
            vkDestroyShaderModule(vkDevice, shaderModule, nullptr);
        }

        commandPool.reset();
        ResourceContextTest::TearDown();
    }

    [[nodiscard]] bool recreateContextWithDeviceAddresses()
    {
        context.reset();
        context = std::make_unique<ResourceContext>();

        const ResourceContextCreateInfo createInfo
        {
            rhiSystem->GetInstance(),
            rhiSystem->GetDevice(),
            rhiSystem->GetVulkanApiVersion(),
            true,
            true,
            k_DefaultMaxFutures,
            k_DefaultStagingRingSize
        };

        return context->Initialize(createInfo) == ResourceContextError::Success;
    }

    [[nodiscard]] VkDeviceAddress queryDeviceAddress(const GraphicsResource& buffer) const
    {
        const VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, reinterpret_cast<VkBuffer>(buffer.VkHandle) };
        return vkGetBufferDeviceAddress(vkDevice, &addressInfo);
    }

    // dispatches the pattern shader with the address of the buffer as a push constant, copies the buffer into a
    // readback buffer on the same command buffer, and returns the words
    [[nodiscard]] std::vector<uint32_t> writePatternThroughAddress(const GraphicsResource& buffer, uint32_t seed)
    {
        const PatternPush push{ buffer.DeviceAddress, seed, 0u };
        GraphicsResource readback = CreateReadbackBuffer(k_BufferBytes);

        // the last submit finished at its fence wait, so the pool can go back to its initial state
        EXPECT_TRUE(commandPool->Reset().IsSuccess());

        rhi::CommandBuffer cmd(commandPool->GetCommandBuffer(0));
        EXPECT_TRUE(cmd.Begin().IsSuccess());

        const VkCommandBuffer vkCmd = cmd.Handle().As<VkCommandBuffer>();
        vkCmdBindPipeline(vkCmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdPushConstants(vkCmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(PatternPush), &push);
        vkCmdDispatch(vkCmd, k_WordCount / k_GroupSize, 1, 1);

        const rhi::GlobalBarrier toCopy{ rhi::PipelineStage::ComputeShader, rhi::Access::ShaderWrite, rhi::PipelineStage::AllTransfer, rhi::Access::TransferRead };
        cmd.Barrier(rhi::DependencyInfo{ std::span{ &toCopy, 1u }, {}, {} });

        const rhi::BufferCopy region{ 0u, 0u, k_BufferBytes };
        cmd.CopyBuffer(rhi::BufferHandle{ buffer.VkHandle }, rhi::BufferHandle{ readback.VkHandle }, std::span{ &region, 1u });

        const rhi::GlobalBarrier toHost{ rhi::PipelineStage::AllTransfer, rhi::Access::TransferWrite, rhi::PipelineStage::Host, rhi::Access::HostRead };
        cmd.Barrier(rhi::DependencyInfo{ std::span{ &toHost, 1u }, {}, {} });

        EXPECT_TRUE(cmd.End().IsSuccess());

        rhi::Fence fence(device->Handle());
        const rhi::CommandBufferHandle cmdHandle = cmd.Handle();
        const rhi::SubmitInfo submit{ {}, std::span{ &cmdHandle, 1u }, {} };
        EXPECT_TRUE(rhi::QueueSubmit(queue, submit, fence.Handle()).IsSuccess());
        EXPECT_TRUE(fence.Wait(k_TimeoutNs));

        return ReadWordsAndDestroy(readback, k_BufferBytes);
    }

    void expectPattern(const std::vector<uint32_t>& words, uint32_t seed) const
    {
        ASSERT_EQ(words.size(), size_t{ k_WordCount });

        for (uint32_t index = 0; index < k_WordCount; ++index)
        {
            ASSERT_EQ(words[index], (index * 2654435761u) ^ seed) << "word " << index;
        }
    }

    rhi::Device* device{ nullptr };
    VkDevice vkDevice{ VK_NULL_HANDLE };
    rhi::QueueHandle queue;
    std::unique_ptr<rhi::CommandPool> commandPool;
    VkShaderModule shaderModule{ VK_NULL_HANDLE };
    VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
    VkPipeline pipeline{ VK_NULL_HANDLE };
};

TEST_F(DeviceAddressTest, AddressIsNonzeroAndMatchesVulkan)
{
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateBuffer(AddressBuffer(k_BufferBytes), {});
    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource buffer = reply.Get();

    EXPECT_NE(buffer.DeviceAddress, 0u);
    EXPECT_EQ(buffer.DeviceAddress, queryDeviceAddress(buffer));

    DestroyAndWait(buffer);
}

TEST_F(DeviceAddressTest, ABufferWithoutTheUsageGetsNoAddress)
{
    BufferCreateInfo info = AddressBuffer(k_BufferBytes);
    info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::TransferSource;

    const GraphicsResource buffer = CreateBufferAndWait(info, {});
    EXPECT_EQ(buffer.DeviceAddress, 0u);

    DestroyAndWait(buffer);
}

TEST_F(DeviceAddressTest, AShaderWritesThroughTheAddress)
{
    const GraphicsResource buffer = CreateBufferAndWait(AddressBuffer(k_BufferBytes), {});
    ASSERT_NE(buffer.DeviceAddress, 0u);

    expectPattern(writePatternThroughAddress(buffer, 0xA5A5u), 0xA5A5u);

    DestroyAndWait(buffer);
}

TEST_F(DeviceAddressTest, TwoBuffersHaveDifferentAddressesAndEachWorks)
{
    const GraphicsResource first = CreateBufferAndWait(AddressBuffer(k_BufferBytes), {});
    const GraphicsResource second = CreateBufferAndWait(AddressBuffer(k_BufferBytes), {});
    ASSERT_NE(first.DeviceAddress, 0u);
    ASSERT_NE(second.DeviceAddress, 0u);
    EXPECT_NE(first.DeviceAddress, second.DeviceAddress);

    // each address reaches its own buffer: one write for each buffer, with its own seed
    expectPattern(writePatternThroughAddress(first, 0x1111u), 0x1111u);
    expectPattern(writePatternThroughAddress(second, 0x2222u), 0x2222u);
    expectPattern(ReadBackWords(first, k_BufferBytes), 0x1111u);

    DestroyAndWait(first);
    DestroyAndWait(second);
}

TEST_F(DeviceAddressTest, APlacedBufferGetsAnAddressThatWorks)
{
    const BufferCreateInfo info = AddressBuffer(k_BufferBytes);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetBufferMemoryRequirements(*context, info);
    ASSERT_NE(requirements.Size, 0u);

    const rhi::placement::MemoryHeapCreateInfo heapInfo{ requirements.Size, requirements.MemoryTypeBits, "address_heap" };
    rhi::ResourceFuture<GraphicsResource> heapReply = rhi::placement::CreateMemoryHeap(*context, heapInfo);
    ASSERT_EQ(context->Wait(heapReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource heap = heapReply.Get();

    rhi::ResourceFuture<GraphicsResource> placedReply = rhi::placement::CreatePlacedBuffer(*context, info, rhi::placement::Placement{ heap, 0u });
    ASSERT_EQ(context->Wait(placedReply, k_TimeoutNs), rhi::FutureState::Complete) << "error: " << static_cast<int>(placedReply.Error());
    const GraphicsResource placed = placedReply.Get();

    EXPECT_NE(placed.DeviceAddress, 0u);
    EXPECT_EQ(placed.DeviceAddress, queryDeviceAddress(placed));
    expectPattern(writePatternThroughAddress(placed, 0x3333u), 0x3333u);

    DestroyAndWait(placed);
    DestroyAndWait(heap);
}

namespace
{
    BufferCreateInfo AddressBuffer(size_t size)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::DeviceAddress | BufferUsageBits::TransferSource;
        info.Size = size;
        return info;
    }
}
