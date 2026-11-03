#include "CommandBuffer.hpp"
#include "CommandPool.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "Queue.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include <cstdint>
#include <gtest/gtest.h>
#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{
    constexpr uint32_t k_Size = 16u;
    constexpr size_t k_ImageBytes = size_t{ k_Size } * k_Size * sizeof(uint32_t);

    /** @brief The usage set of the images in this file. UavComputeShader is deliberately absent, so that it is the usage
     *  that these images were not created with. */
    constexpr ImageUsageBits k_ImageUsage = ImageUsageBits::SampledImage | ImageUsageBits::TransferDestination | ImageUsageBits::TransferSource;

    constexpr rhi::ImageUsageTransition k_SampledToSampled{ ImageUsageBits::SampledImage, ImageUsageBits::SampledImage };

    [[nodiscard]] ImageCreateInfo HandoffImageInfo(ImageUsageBits usage);
    [[nodiscard]] BufferCreateInfo DeviceLocalCopyBuffer(size_t size);
    [[nodiscard]] RhiImageResourceData WholeImageData(const std::vector<uint32_t>& texels);
    /** @brief Clears mip 0 of the image on the general queue and waits for the fence. The image goes from UNDEFINED to
     *  TRANSFER_DST_OPTIMAL and stays there. */
    [[nodiscard]] bool ClearImageOnGeneralQueue(const rhi::Device& device, const GraphicsResource& image, const VkClearColorValue& color);
}

TEST_F(ResourceContextTest, UploadLeavesTheImageInTheRequestedUsage)
{
    // the create usage has a shader write bit, so a layout derived from the whole create usage would be GENERAL. the
    // caller asked for SampledImage, and the readback below says the image is in that usage's layout
    const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 21u);
    const RhiImageResourceData data = WholeImageData(texels);

    ImageCreateInfo info = HandoffImageInfo(k_ImageUsage | ImageUsageBits::UavComputeShader);
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = reply.Get();

    EXPECT_EQ(ReadBackImageWords(image, k_SampledToSampled, k_ImageBytes), texels);
    DestroyAndWait(image);
}

TEST_F(ResourceContextTest, CopyOutAfterGpuWorkStartsFromTheCallersUsage)
{
    const ImageCreateInfo info = HandoffImageInfo(k_ImageUsage);
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(info);
    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = reply.Get();

    // red, in the 8-bit unorm format: the word is 0xFF0000FF with the red byte first in memory
    constexpr VkClearColorValue k_Red{ { 1.0f, 0.0f, 0.0f, 1.0f } };
    constexpr uint32_t k_RedWord = 0xFF0000FFu;
    ASSERT_TRUE(ClearImageOnGeneralQueue(*rhiSystem->GetDevice(), image, k_Red));

    // the clear left the image in TRANSFER_DST_OPTIMAL, and the copy out ends in the sampled layout
    const rhi::ImageUsageTransition clearedToSampled{ ImageUsageBits::TransferDestination, ImageUsageBits::SampledImage };
    const std::vector<uint32_t> words = ReadBackImageWords(image, clearedToSampled, k_ImageBytes);
    EXPECT_EQ(words, std::vector<uint32_t>(k_Size * k_Size, k_RedWord));

    DestroyAndWait(image);
}

TEST_F(ResourceContextTest, SetImageDataTwiceThenReadBack)
{
    const std::vector<uint32_t> first = MakeWordPattern(k_Size * k_Size, 31u);
    const std::vector<uint32_t> second = MakeWordPattern(k_Size * k_Size, 32u);
    const std::vector<uint32_t> third = MakeWordPattern(k_Size * k_Size, 33u);
    const RhiImageResourceData firstData = WholeImageData(first);
    const RhiImageResourceData secondData = WholeImageData(second);
    const RhiImageResourceData thirdData = WholeImageData(third);

    const ImageCreateInfo info = HandoffImageInfo(k_ImageUsage);
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(info, std::span{ &firstData, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = reply.Get();

    rhi::OperationFuture secondReply = context->SetImageData(image, k_SampledToSampled, std::span{ &secondData, 1u }, {});
    ASSERT_EQ(context->Wait(secondReply, k_TimeoutNs), rhi::FutureState::Complete);
    rhi::OperationFuture thirdReply = context->SetImageData(image, k_SampledToSampled, std::span{ &thirdData, 1u }, {});
    ASSERT_EQ(context->Wait(thirdReply, k_TimeoutNs), rhi::FutureState::Complete);

    EXPECT_EQ(ReadBackImageWords(image, k_SampledToSampled, k_ImageBytes), third);
    DestroyAndWait(image);
}

TEST_F(ResourceContextTest, DiscardUploadReplacesTheWholeImage)
{
    const std::vector<uint32_t> first = MakeWordPattern(k_Size * k_Size, 51u);
    const std::vector<uint32_t> second = MakeWordPattern(k_Size * k_Size, 52u);
    const RhiImageResourceData firstData = WholeImageData(first);
    const RhiImageResourceData secondData = WholeImageData(second);

    const ImageCreateInfo info = HandoffImageInfo(k_ImageUsage);
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(info, std::span{ &firstData, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = reply.Get();

    // the image is in the sampled layout, and the caller says that its contents do not matter
    const rhi::ImageUsageTransition discardToSampled{ ImageUsageBits::Discard, ImageUsageBits::SampledImage };
    rhi::OperationFuture secondReply = context->SetImageData(image, discardToSampled, std::span{ &secondData, 1u }, {});
    ASSERT_EQ(context->Wait(secondReply, k_TimeoutNs), rhi::FutureState::Complete);

    EXPECT_EQ(ReadBackImageWords(image, k_SampledToSampled, k_ImageBytes), second);
    DestroyAndWait(image);
}

TEST_F(ResourceContextTest, DiscardOnACopyDestination)
{
    const std::vector<uint32_t> sourceTexels = MakeWordPattern(k_Size * k_Size, 61u);
    const std::vector<uint32_t> oldTexels = MakeWordPattern(k_Size * k_Size, 62u);
    const RhiImageResourceData sourceData = WholeImageData(sourceTexels);
    const RhiImageResourceData oldData = WholeImageData(oldTexels);

    const ImageCreateInfo info = HandoffImageInfo(k_ImageUsage);
    rhi::ResourceFuture<GraphicsResource> sourceReply = context->CreateImage(info, std::span{ &sourceData, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(sourceReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource source = sourceReply.Get();

    rhi::ResourceFuture<GraphicsResource> destinationReply = context->CreateImage(info, std::span{ &oldData, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(destinationReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource destination = destinationReply.Get();

    const rhi::ImageUsageTransition discardToSampled{ ImageUsageBits::Discard, ImageUsageBits::SampledImage };
    rhi::OperationFuture copyReply = context->CopyImageContents(source, k_SampledToSampled, destination, discardToSampled, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    EXPECT_EQ(ReadBackImageWords(destination, k_SampledToSampled, k_ImageBytes), sourceTexels);
    EXPECT_EQ(ReadBackImageWords(source, k_SampledToSampled, k_ImageBytes), sourceTexels);

    DestroyAndWait(source);
    DestroyAndWait(destination);
}

TEST_F(ResourceContextTest, DiscardRefusals)
{
    const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 71u);
    const RhiImageResourceData data = WholeImageData(texels);

    const ImageCreateInfo info = HandoffImageInfo(k_ImageUsage);
    rhi::ResourceFuture<GraphicsResource> imageReply = context->CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(imageReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = imageReply.Get();

    rhi::ResourceFuture<GraphicsResource> otherReply = context->CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(otherReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource other = otherReply.Get();

    const GraphicsResource readback = CreateReadbackBuffer(k_ImageBytes);

    const rhi::ImageUsageTransition discardAfter{ ImageUsageBits::SampledImage, ImageUsageBits::Discard };
    const rhi::ImageUsageTransition discardBoth{ ImageUsageBits::Discard, ImageUsageBits::Discard };
    const rhi::ImageUsageTransition discardWithSampled{ ImageUsageBits::Discard | ImageUsageBits::SampledImage, ImageUsageBits::SampledImage };
    const rhi::ImageUsageTransition afterWithDiscard{ ImageUsageBits::Discard, ImageUsageBits::SampledImage | ImageUsageBits::Discard };
    // a transfer that reads the image needs defined contents, so a discard before is refused on a source
    const rhi::ImageUsageTransition discardSource{ ImageUsageBits::Discard, ImageUsageBits::SampledImage };
    ImageCreateInfo discardInfo = info;
    discardInfo.Usage |= ImageUsageBits::Discard;

    struct Case
    {
        const char* Name;
        rhi::OperationFuture Future;
        rhi::ResourceError Expected;
    };

    std::vector<Case> cases;
    cases.push_back({ "set data with a discard after", context->SetImageData(image, discardAfter, std::span{ &data, 1u }, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "set data with a discard before and after", context->SetImageData(image, discardBoth, std::span{ &data, 1u }, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "set data with a discard and sampled before", context->SetImageData(image, discardWithSampled, std::span{ &data, 1u }, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "set data with a discard and sampled after", context->SetImageData(image, afterWithDiscard, std::span{ &data, 1u }, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "copy out with a discard after", context->CopyImageToBuffer(image, discardAfter, readback, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "image copy with a discard destination after", context->CopyImageContents(image, k_SampledToSampled, other, discardAfter, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "new image copy with a discard resting usage", context->CopyImage(image, k_SampledToSampled, ImageUsageBits::Discard, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "copy out with a discard source", context->CopyImageToBuffer(image, discardSource, readback, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "image copy with a discard source", context->CopyImageContents(image, discardSource, other, k_SampledToSampled, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "new image copy with a discard source", context->CopyImage(image, discardSource, ImageUsageBits::SampledImage, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "create with a discard in the usage", context->CreateImage(discardInfo), rhi::ResourceError::InvalidCreateInfo });
    cases.push_back({ "create with a discard in the usage and data", context->CreateImage(discardInfo, std::span{ &data, 1u }, ImageUsageBits::SampledImage), rhi::ResourceError::InvalidCreateInfo });
    cases.push_back({ "create with a discard after upload", context->CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::Discard), rhi::ResourceError::InvalidCreateInfo });

    for (const Case& entry : cases)
    {
        EXPECT_EQ(context->Wait(entry.Future, k_TimeoutNs), rhi::FutureState::Failed) << entry.Name;
        EXPECT_EQ(entry.Future.Error(), entry.Expected)
            << entry.Name << ": got " << rhi::ToString(entry.Future.Error()) << ", expected " << rhi::ToString(entry.Expected);
    }

    cases.clear();

    // the refused calls changed nothing
    EXPECT_EQ(ReadBackImageWords(image, k_SampledToSampled, k_ImageBytes), texels);

    for (const GraphicsResource& resource : { image, other, readback })
    {
        DestroyAndWait(resource);
    }
}

TEST_F(ResourceContextTest, UsageTransitionErrors)
{
    const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 41u);
    const RhiImageResourceData data = WholeImageData(texels);

    const ImageCreateInfo info = HandoffImageInfo(k_ImageUsage);
    rhi::ResourceFuture<GraphicsResource> imageReply = context->CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(imageReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = imageReply.Get();

    rhi::ResourceFuture<GraphicsResource> otherReply = context->CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(otherReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource other = otherReply.Get();

    const GraphicsResource readback = CreateReadbackBuffer(k_ImageBytes);
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalCopyBuffer(k_ImageBytes), {});
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalCopyBuffer(k_ImageBytes), {});

    // UavComputeShader is not in the create usage of the images
    const rhi::ImageUsageTransition zeroBefore{ ImageUsageBits::Invalid, ImageUsageBits::SampledImage };
    const rhi::ImageUsageTransition zeroAfter{ ImageUsageBits::SampledImage, ImageUsageBits::Invalid };
    const rhi::ImageUsageTransition unknownBefore{ ImageUsageBits::UavComputeShader, ImageUsageBits::SampledImage };
    const rhi::ImageUsageTransition unknownAfter{ ImageUsageBits::SampledImage, ImageUsageBits::UavComputeShader };

    struct Case
    {
        const char* Name;
        rhi::OperationFuture Future;
        rhi::ResourceError Expected;
    };

    std::vector<Case> cases;
    cases.push_back({ "create with a zero usage after upload", context->CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::Invalid), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "create with a usage the image lacks", context->CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::UavComputeShader), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "set data with a zero before", context->SetImageData(image, zeroBefore, std::span{ &data, 1u }, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "set data with a zero after", context->SetImageData(image, zeroAfter, std::span{ &data, 1u }, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "set data with a before the image lacks", context->SetImageData(image, unknownBefore, std::span{ &data, 1u }, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "set data with an after the image lacks", context->SetImageData(image, unknownAfter, std::span{ &data, 1u }, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "copy out with a zero before", context->CopyImageToBuffer(image, zeroBefore, readback, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "copy out with an after the image lacks", context->CopyImageToBuffer(image, unknownAfter, readback, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "copy in with a before the image lacks", context->CopyBufferToImage(source, image, unknownBefore, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "copy in with a zero after", context->CopyBufferToImage(source, image, zeroAfter, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "image copy with a source before the image lacks", context->CopyImageContents(image, unknownBefore, other, k_SampledToSampled, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "image copy with a zero destination after", context->CopyImageContents(image, k_SampledToSampled, other, zeroAfter, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "new image copy with a zero source before", context->CopyImage(image, zeroBefore, ImageUsageBits::SampledImage, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "new image copy with a usage the image lacks", context->CopyImage(image, k_SampledToSampled, ImageUsageBits::UavComputeShader, {}), rhi::ResourceError::InvalidUsageTransition });
    cases.push_back({ "buffer copy of an image, image first", context->CopyBufferContents(image, buffer, {}), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "buffer copy of an image, buffer first", context->CopyBufferContents(buffer, image, {}), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "buffer copy from an image to an image", context->CopyBufferContents(image, other, {}), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "copy out into an image", context->CopyImageToBuffer(image, k_SampledToSampled, other, {}), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "copy in from an image", context->CopyBufferToImage(other, image, k_SampledToSampled, {}), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "image copy from a buffer", context->CopyImageContents(buffer, k_SampledToSampled, image, k_SampledToSampled, {}), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "buffer copy API on an image", context->CopyBuffer(image, true, {}), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "image copy API on a buffer", context->CopyImage(buffer, k_SampledToSampled, ImageUsageBits::SampledImage, {}), rhi::ResourceError::WrongResourceKind });

    for (const Case& entry : cases)
    {
        EXPECT_EQ(context->Wait(entry.Future, k_TimeoutNs), rhi::FutureState::Failed) << entry.Name;
        EXPECT_EQ(entry.Future.Error(), entry.Expected)
            << entry.Name << ": got " << rhi::ToString(entry.Future.Error()) << ", expected " << rhi::ToString(entry.Expected);
    }

    cases.clear();

    // the refused calls changed nothing: the image still holds the first upload, in the layout the caller said
    EXPECT_EQ(ReadBackImageWords(image, k_SampledToSampled, k_ImageBytes), texels);

    for (const GraphicsResource& resource : { image, other, readback, source, buffer })
    {
        DestroyAndWait(resource);
    }
}

namespace
{
    ImageCreateInfo HandoffImageInfo(ImageUsageBits usage)
    {
        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = usage;
        info.Type = ImageType::Type2D;
        info.Format = ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
        info.Width = k_Size;
        info.Height = k_Size;
        info.Depth = 1u;
        return info;
    }

    BufferCreateInfo DeviceLocalCopyBuffer(size_t size)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::CopySource | BufferUsageBits::CopyDestination;
        info.Size = size;
        return info;
    }

    RhiImageResourceData WholeImageData(const std::vector<uint32_t>& texels)
    {
        RhiImageResourceData data{};
        data.Data = std::as_bytes(std::span{ texels });
        data.Width = k_Size;
        data.Height = k_Size;
        return data;
    }

    bool ClearImageOnGeneralQueue(const rhi::Device& device, const GraphicsResource& image, const VkClearColorValue& color)
    {
        rhi::CommandPool commandPool(device.Handle(), rhi::CommandPool::Type::Compute, device.GetQueueFamilyIndices().Graphics);
        if (!commandPool.AllocateCommandBuffers(1).IsSuccess())
        {
            return false;
        }

        rhi::CommandBuffer commandBuffer(commandPool.GetCommandBuffer(0));
        if (!commandBuffer.Begin().IsSuccess())
        {
            return false;
        }

        const VkCommandBuffer cmd = commandBuffer.Handle().As<VkCommandBuffer>();
        const VkImage vkImage = reinterpret_cast<VkImage>(image.VkHandle);
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };

        const VkImageMemoryBarrier2 toClear
        {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            nullptr,
            VK_PIPELINE_STAGE_2_NONE,
            VK_ACCESS_2_NONE,
            VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            vkImage,
            range
        };
        const VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO, nullptr, 0, 0, nullptr, 0, nullptr, 1, &toClear };
        vkCmdPipelineBarrier2(cmd, &dependency);
        vkCmdClearColorImage(cmd, vkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1u, &range);

        if (!commandBuffer.End().IsSuccess())
        {
            return false;
        }

        const rhi::CommandBufferHandle commandBufferHandle = commandBuffer.Handle();
        rhi::Fence fence(device.Handle());
        const rhi::SubmitInfo submit{ {}, std::span{ &commandBufferHandle, 1 }, {} };
        if (!rhi::QueueSubmit(device.GetGeneralQueue(), submit, fence.Handle()).IsSuccess())
        {
            return false;
        }

        return fence.Wait();
    }
}
