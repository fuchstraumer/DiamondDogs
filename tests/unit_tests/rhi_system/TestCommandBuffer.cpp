#include <gtest/gtest.h>
#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "Queue.hpp"
#include "RhiFlags.hpp"
#include "RhiResult.hpp"
#include "RhiSystem.hpp"
#include "RhiTypes.hpp"
#include "Semaphore.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;

    struct TestBuffer
    {
        VkBuffer Buffer{ VK_NULL_HANDLE };
        VkDeviceMemory Memory{ VK_NULL_HANDLE };
        [[nodiscard]] rhi::BufferHandle Handle() const noexcept { return rhi::BufferHandle{ reinterpret_cast<uint64_t>(Buffer) }; }
    };

    struct TestImage
    {
        VkImage Image{ VK_NULL_HANDLE };
        VkImageView View{ VK_NULL_HANDLE };
        VkDeviceMemory Memory{ VK_NULL_HANDLE };
    };
}

// Records through rhi::CommandBuffer and submits through rhi::QueueSubmit, with synchronization validation on, so a
// missing or wrong barrier shows up as a validation message (which the test runner counts as a failure)
class CommandBufferTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        rhi::RhiSystemCreateInfo createInfo{};
        createInfo.ApplicationName = "CommandBufferTest";
        createInfo.EngineName = "DiamondDogsTestEngine";
        createInfo.VkVersion = rhi::ApiVersion::Vulkan13;
        createInfo.ValidationLevel = rhi::ValidationLayers::WithSynchronization;
        createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
        createInfo.ShaderCacheDir = (std::filesystem::temp_directory_path() / "DiamondDogsUnitTesting" / "ShaderCache").string();
        rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
        device = rhiSystem->GetDevice();
        ASSERT_NE(device, nullptr);
        vkDevice = device->Handle().As<VkDevice>();
        queue = device->GetGeneralQueue();
        commandPool = std::make_unique<rhi::CommandPool>(device->Handle(), rhi::CommandPool::Type::Graphics, device->GetQueueFamilyIndices().Graphics);
        ASSERT_TRUE(commandPool->AllocateCommandBuffers(1).IsSuccess());
    }

    void TearDown() override
    {
        if (device != nullptr)
        {
            device->WaitDeviceIdle();
        }
        for (const TestBuffer& buffer : buffers)
        {
            vkDestroyBuffer(vkDevice, buffer.Buffer, nullptr);
            vkFreeMemory(vkDevice, buffer.Memory, nullptr);
        }
        for (const TestImage& image : images)
        {
            vkDestroyImageView(vkDevice, image.View, nullptr);
            vkDestroyImage(vkDevice, image.Image, nullptr);
            vkFreeMemory(vkDevice, image.Memory, nullptr);
        }
        commandPool.reset();
        rhiSystem.reset();
    }

    VkDeviceMemory allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties)
    {
        const VkMemoryAllocateInfo allocInfo
        {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            requirements.size,
            device->GetMemoryTypeIndex(requirements.memoryTypeBits, properties)
        };
        VkDeviceMemory memory = VK_NULL_HANDLE;
        EXPECT_EQ(vkAllocateMemory(vkDevice, &allocInfo, nullptr, &memory), VK_SUCCESS);
        return memory;
    }

    const TestBuffer& createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
    {
        TestBuffer& buffer = buffers.emplace_back();
        const VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, size, usage, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr };
        EXPECT_EQ(vkCreateBuffer(vkDevice, &bufferInfo, nullptr, &buffer.Buffer), VK_SUCCESS);
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(vkDevice, buffer.Buffer, &requirements);
        buffer.Memory = allocate(requirements, properties);
        EXPECT_EQ(vkBindBufferMemory(vkDevice, buffer.Buffer, buffer.Memory, 0), VK_SUCCESS);
        return buffer;
    }

    const TestImage& createColorImage(uint32_t width, uint32_t height)
    {
        TestImage& image = images.emplace_back();
        const VkImageCreateInfo imageInfo
        {
            VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { width, height, 1 }, 1, 1,
            VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED
        };
        EXPECT_EQ(vkCreateImage(vkDevice, &imageInfo, nullptr, &image.Image), VK_SUCCESS);
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(vkDevice, image.Image, &requirements);
        image.Memory = allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        EXPECT_EQ(vkBindImageMemory(vkDevice, image.Image, image.Memory, 0), VK_SUCCESS);
        const VkImageViewCreateInfo viewInfo
        {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, image.Image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {},
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        EXPECT_EQ(vkCreateImageView(vkDevice, &viewInfo, nullptr, &image.View), VK_SUCCESS);
        return image;
    }

    std::vector<uint32_t> readWords(const TestBuffer& buffer, size_t count)
    {
        std::vector<uint32_t> words(count);
        void* mapped = nullptr;
        EXPECT_EQ(vkMapMemory(vkDevice, buffer.Memory, 0, VK_WHOLE_SIZE, 0, &mapped), VK_SUCCESS);
        std::memcpy(words.data(), mapped, count * sizeof(uint32_t));
        vkUnmapMemory(vkDevice, buffer.Memory);
        return words;
    }

    static constexpr VkMemoryPropertyFlags k_HostVisible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    // makes transfer writes visible to a host read after the fence wait
    static constexpr rhi::GlobalBarrier k_TransferToHost{ rhi::PipelineStage::AllTransfer, rhi::Access::TransferWrite, rhi::PipelineStage::Host, rhi::Access::HostRead };

    std::unique_ptr<rhi::RhiSystem> rhiSystem;
    rhi::Device* device{ nullptr };
    VkDevice vkDevice{ VK_NULL_HANDLE };
    rhi::QueueHandle queue;
    std::unique_ptr<rhi::CommandPool> commandPool;
    std::deque<TestBuffer> buffers;
    std::deque<TestImage> images;
};

TEST_F(CommandBufferTest, FillCopyAndSignalTimeline)
{
    constexpr size_t k_WordCount = 256;
    constexpr VkDeviceSize k_Bytes = k_WordCount * sizeof(uint32_t);
    constexpr uint32_t k_Pattern = 0xDEADBEEFu;
    const TestBuffer& source = createBuffer(k_Bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const TestBuffer& readback = createBuffer(k_Bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, k_HostVisible);

    rhi::CommandBuffer cmd(commandPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());
    cmd.FillBuffer(source.Handle(), 0, rhi::k_WholeSize, k_Pattern);
    const rhi::BufferBarrier fillToCopy
    {
        source.Handle(),
        rhi::PipelineStage::Clear, rhi::Access::TransferWrite,
        rhi::PipelineStage::Copy, rhi::Access::TransferRead
    };
    cmd.Barrier(rhi::DependencyInfo{ {}, std::span{ &fillToCopy, 1 }, {} });
    const rhi::BufferCopy region{ 0, 0, k_Bytes };
    cmd.CopyBuffer(source.Handle(), readback.Handle(), std::span{ &region, 1 });
    cmd.Barrier(rhi::DependencyInfo{ std::span{ &k_TransferToHost, 1 }, {}, {} });
    ASSERT_TRUE(cmd.End().IsSuccess());

    rhi::TimelineSemaphore timeline(device->Handle());
    rhi::Fence fence(device->Handle());
    const rhi::CommandBufferHandle cmdHandle = cmd.Handle();
    const rhi::SemaphoreSubmitInfo signal{ timeline.Handle(), 1u, rhi::PipelineStage::AllCommands };
    const rhi::SubmitInfo submit{ {}, std::span{ &cmdHandle, 1 }, std::span{ &signal, 1 } };
    ASSERT_TRUE(rhi::QueueSubmit(queue, submit, fence.Handle()).IsSuccess());
    ASSERT_TRUE(fence.Wait(k_TimeoutNs));

    uint64_t value = 0;
    ASSERT_TRUE(timeline.GetValue(value).IsSuccess());
    EXPECT_EQ(value, 1u);
    for (const uint32_t word : readWords(readback, k_WordCount))
    {
        ASSERT_EQ(word, k_Pattern);
    }
}

TEST_F(CommandBufferTest, ClearRenderAndReadBack)
{
    constexpr uint32_t k_Size = 4;
    const TestImage& image = createColorImage(k_Size, k_Size);
    const TestBuffer& readback = createBuffer(k_Size * k_Size * sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT, k_HostVisible);
    const rhi::ImageHandle imageHandle{ reinterpret_cast<uint64_t>(image.Image) };

    rhi::CommandBuffer cmd(commandPool->GetCommandBuffer(0));
    ASSERT_TRUE(cmd.Begin().IsSuccess());
    const rhi::ImageBarrier toAttachment
    {
        imageHandle,
        rhi::PipelineStage::None, rhi::Access::None,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
        rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment
    };
    cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toAttachment, 1 } });

    rhi::ColorAttachment attachment{};
    attachment.ImageView = rhi::ImageViewHandle{ reinterpret_cast<uint64_t>(image.View) };
    attachment.ClearValue = rhi::ClearColorValue{ 1.0f, 0.0f, 1.0f, 1.0f };
    rhi::RenderingInfo renderingInfo{};
    renderingInfo.ColorAttachments = std::span{ &attachment, 1 };
    renderingInfo.RenderArea = rhi::Rect2D{ 0, 0, k_Size, k_Size };
    cmd.BeginRendering(renderingInfo);
    cmd.EndRendering();

    const rhi::ImageBarrier toTransfer
    {
        imageHandle,
        rhi::PipelineStage::ColorAttachmentOutput, rhi::Access::ColorAttachmentWrite,
        rhi::PipelineStage::Copy, rhi::Access::TransferRead,
        rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::TransferSource
    };
    cmd.Barrier(rhi::DependencyInfo{ {}, {}, std::span{ &toTransfer, 1 } });
    rhi::BufferImageCopy region{};
    region.Width = k_Size;
    region.Height = k_Size;
    cmd.CopyImageToBuffer(imageHandle, rhi::ImageLayout::TransferSource, readback.Handle(), std::span{ &region, 1 });
    cmd.Barrier(rhi::DependencyInfo{ std::span{ &k_TransferToHost, 1 }, {}, {} });
    ASSERT_TRUE(cmd.End().IsSuccess());

    rhi::Fence fence(device->Handle());
    const rhi::CommandBufferHandle cmdHandle = cmd.Handle();
    ASSERT_TRUE(rhi::QueueSubmit(queue, rhi::SubmitInfo{ {}, std::span{ &cmdHandle, 1 }, {} }, fence.Handle()).IsSuccess());
    ASSERT_TRUE(fence.Wait(k_TimeoutNs));

    // RGBA8 magenta, little endian
    for (const uint32_t pixel : readWords(readback, k_Size * k_Size))
    {
        ASSERT_EQ(pixel, 0xFFFF00FFu);
    }
}

TEST_F(CommandBufferTest, BatchesChainThroughTimelineWaits)
{
    rhi::TimelineSemaphore timeline(device->Handle());
    rhi::Fence fence(device->Handle());
    const rhi::SemaphoreSubmitInfo signalOne{ timeline.Handle(), 1u, rhi::PipelineStage::AllCommands };
    const rhi::SemaphoreSubmitInfo waitOne{ timeline.Handle(), 1u, rhi::PipelineStage::AllCommands };
    const rhi::SemaphoreSubmitInfo signalTwo{ timeline.Handle(), 2u, rhi::PipelineStage::AllCommands };
    const std::array<rhi::SubmitInfo, 2> batches
    {
        rhi::SubmitInfo{ {}, {}, std::span{ &signalOne, 1 } },
        rhi::SubmitInfo{ std::span{ &waitOne, 1 }, {}, std::span{ &signalTwo, 1 } },
    };
    ASSERT_TRUE(rhi::QueueSubmit(queue, batches, fence.Handle()).IsSuccess());
    ASSERT_TRUE(fence.Wait(k_TimeoutNs));
    uint64_t value = 0;
    ASSERT_TRUE(timeline.GetValue(value).IsSuccess());
    EXPECT_EQ(value, 2u);
    EXPECT_TRUE(rhi::QueueWaitIdle(queue).IsSuccess());
}
