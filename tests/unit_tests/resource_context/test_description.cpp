#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFuture.hpp"
#include "ResourcePlacement.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    constexpr uint32_t k_Size = 16u;
    constexpr uint64_t k_PollLimitNs = 5'000'000'000ull;
    constexpr rhi::ImageUsageTransition k_SampledToSampled{ ImageUsageBits::SampledImage, ImageUsageBits::SampledImage };

    /** @brief Polls until the future leaves Pending, and returns the first state that it saw after that. Returns
     *  Pending when the limit passes. */
    [[nodiscard]] rhi::FutureState PollUntilCreated(const rhi::OperationFuture& future);
    [[nodiscard]] ImageCreateInfo DescriptionImageInfo(ImageUsageBits usage);
    [[nodiscard]] BufferCreateInfo DescriptionBufferInfo(ResourceDomain domain, size_t size);
    [[nodiscard]] RhiImageResourceData WholeImageData(const std::vector<uint32_t>& texels);
    [[nodiscard]] bool IsCreatedOrComplete(rhi::FutureState state) noexcept;
    /** @brief Creates one image with data, and waits for it. */
    [[nodiscard]] GraphicsResource MakeImageWithData(ResourceContext& context, const ImageCreateInfo& info, const std::vector<uint32_t>& texels);
}

TEST_F(ResourceContextTest, DescriptionOfDeviceLocalBufferIsValidAtFirstCreated)
{
    const std::vector<uint32_t> words = MakeWordPattern(256u, 3u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    BufferCreateInfo info = DescriptionBufferInfo(ResourceDomain::GPUOnly, data.Data.size());
    info.Usage = BufferUsageBits::ComputeShaderSRV;

    rhi::ResourceFuture<GraphicsResource> reply = context->CreateBuffer(info, std::span{ &data, 1u });
    const rhi::FutureState firstState = PollUntilCreated(reply);
    ASSERT_TRUE(IsCreatedOrComplete(firstState));

    // the context adds the destination bit that the staged upload needs, and the description reports it
    const ResourceDescription& description = reply.Description();
    EXPECT_EQ(description.Type, ResourceType::Buffer);
    EXPECT_EQ(description.Domain, ResourceDomain::GPUOnly);
    EXPECT_EQ(description.BufferUsage, BufferUsageBits::ComputeShaderSRV | BufferUsageBits::TransferDestination);
    EXPECT_EQ(description.ByteSize, uint64_t{ 256u * sizeof(uint32_t) });
    EXPECT_EQ(description.ImageUsage, ImageUsageBits::Invalid);
    EXPECT_EQ(description.InitialUsage, ImageUsageBits::Invalid);

    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(reply.Get());
}

TEST_F(ResourceContextTest, DescriptionOfHostVisibleBuffer)
{
    BufferCreateInfo info = DescriptionBufferInfo(ResourceDomain::CPUToGPU, 1000u);
    info.Usage = BufferUsageBits::ComputeShaderUBO;

    rhi::ResourceFuture<GraphicsResource> reply = context->CreateBuffer(info, {});
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(reply)));

    const ResourceDescription& description = reply.Description();
    EXPECT_EQ(description.Type, ResourceType::Buffer);
    EXPECT_EQ(description.Domain, ResourceDomain::CPUToGPU);
    EXPECT_EQ(description.BufferUsage, BufferUsageBits::ComputeShaderUBO);
    EXPECT_EQ(description.ByteSize, 1000u);

    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(reply.Get());
}

TEST_F(ResourceContextTest, DescriptionOfImageWithoutData)
{
    ImageCreateInfo info = DescriptionImageInfo(ImageUsageBits::SampledImage | ImageUsageBits::ComputeShaderSRV);
    info.Width = 32u;
    info.Height = 16u;
    info.MipLevels = 3u;
    info.ArrayLayers = 2u;

    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(info);
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(reply)));

    const ResourceDescription& description = reply.Description();
    EXPECT_EQ(description.Type, ResourceType::Image);
    EXPECT_EQ(description.Domain, ResourceDomain::GPUOnly);
    EXPECT_EQ(description.ImageUsage, ImageUsageBits::SampledImage | ImageUsageBits::ComputeShaderSRV);
    EXPECT_EQ(description.Dimensions, ImageType::Type2D);
    EXPECT_TRUE(description.Format == info.Format);
    EXPECT_EQ(description.Width, 32);
    EXPECT_EQ(description.Height, 16);
    EXPECT_EQ(description.Depth, 1);
    EXPECT_EQ(description.MipLevels, 3);
    EXPECT_EQ(description.ArrayLayers, 2);
    EXPECT_EQ(description.SampleCount, 1);
    EXPECT_EQ(description.InitialUsage, ImageUsageBits::Invalid);
    EXPECT_EQ(description.BufferUsage, BufferUsageBits::Invalid);
    EXPECT_EQ(description.ByteSize, 0u);

    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(reply.Get());
}

TEST_F(ResourceContextTest, DescriptionOfCombinedImageSamplerIsAnImage)
{
    ImageCreateInfo info = DescriptionImageInfo(ImageUsageBits::SampledImage);
    info.ViewInfo = ImageViewCreateInfo{};
    info.SamplerInfo = SamplerCreateInfo{};

    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(info);
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(reply)));

    const ResourceDescription& description = reply.Description();
    EXPECT_EQ(description.Type, ResourceType::CombinedImageSampler);
    EXPECT_EQ(description.Width, static_cast<int32_t>(k_Size));
    EXPECT_EQ(description.InitialUsage, ImageUsageBits::Invalid);

    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(reply.Get());
}

TEST_F(ResourceContextTest, DescriptionOfImageWithDataHasTheUploadUsage)
{
    const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 7u);
    const RhiImageResourceData data = WholeImageData(texels);
    const ImageCreateInfo info = DescriptionImageInfo(ImageUsageBits::SampledImage | ImageUsageBits::TransferSource);

    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::SampledImage);
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(reply)));

    const ResourceDescription& description = reply.Description();
    EXPECT_EQ(description.Type, ResourceType::Image);
    EXPECT_EQ(description.InitialUsage, ImageUsageBits::SampledImage);
    EXPECT_NE(description.ImageUsage & ImageUsageBits::TransferDestination, ImageUsageBits::Invalid);
    EXPECT_NE(description.ImageUsage & ImageUsageBits::SampledImage, ImageUsageBits::Invalid);
    EXPECT_EQ(description.Width, static_cast<int32_t>(k_Size));
    EXPECT_EQ(description.Height, static_cast<int32_t>(k_Size));

    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(reply.Get());
}

TEST_F(ResourceContextTest, DescriptionOfSampler)
{
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateSampler(SamplerCreateInfo{});
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(reply)));

    const ResourceDescription& description = reply.Description();
    EXPECT_EQ(description.Type, ResourceType::Sampler);
    EXPECT_EQ(description.Domain, ResourceDomain::Invalid);
    EXPECT_EQ(description.BufferUsage, BufferUsageBits::Invalid);
    EXPECT_EQ(description.ByteSize, 0u);
    EXPECT_EQ(description.ImageUsage, ImageUsageBits::Invalid);
    EXPECT_EQ(description.Width, 0);
    EXPECT_EQ(description.MipLevels, 0);
    EXPECT_EQ(description.InitialUsage, ImageUsageBits::Invalid);

    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(reply.Get());
}

TEST_F(ResourceContextTest, DescriptionOfPlacedBufferAndPlacedImage)
{
    BufferCreateInfo bufferInfo = DescriptionBufferInfo(ResourceDomain::GPUOnly, 4096u);
    bufferInfo.Usage = BufferUsageBits::TransferSource | BufferUsageBits::TransferDestination;
    ImageCreateInfo imageInfo = DescriptionImageInfo(ImageUsageBits::SampledImage | ImageUsageBits::TransferDestination);
    imageInfo.MipLevels = 2u;

    const rhi::placement::MemoryRequirements bufferRequirements = rhi::placement::GetBufferMemoryRequirements(*context, bufferInfo);
    const rhi::placement::MemoryRequirements imageRequirements = rhi::placement::GetImageMemoryRequirements(*context, imageInfo);
    const uint64_t heapSize = bufferRequirements.Size + imageRequirements.Size + bufferRequirements.Alignment + imageRequirements.Alignment;
    const uint32_t memoryTypeBits = bufferRequirements.MemoryTypeBits & imageRequirements.MemoryTypeBits;
    ASSERT_NE(memoryTypeBits, 0u);

    rhi::ResourceFuture<GraphicsResource> heapReply = rhi::placement::CreateMemoryHeap(*context, rhi::placement::MemoryHeapCreateInfo{ heapSize, memoryTypeBits, "description_heap" });
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(heapReply)));
    EXPECT_EQ(heapReply.Description().Type, ResourceType::Heap);
    EXPECT_EQ(heapReply.Description().ByteSize, heapSize);

    // the image goes after the buffer, at the next offset that suits its alignment
    const uint64_t imageOffset = ((bufferRequirements.Size + imageRequirements.Alignment - 1u) / imageRequirements.Alignment) * imageRequirements.Alignment;

    rhi::ResourceFuture<GraphicsResource> bufferReply = rhi::placement::CreatePlacedBuffer(*context, bufferInfo, rhi::placement::Placement{ heapReply.Get(), 0u });
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(bufferReply)));
    EXPECT_EQ(bufferReply.Description().Type, ResourceType::Buffer);
    EXPECT_EQ(bufferReply.Description().Domain, ResourceDomain::GPUOnly);
    EXPECT_EQ(bufferReply.Description().BufferUsage, bufferInfo.Usage);
    EXPECT_EQ(bufferReply.Description().ByteSize, 4096u);

    rhi::ResourceFuture<GraphicsResource> imageReply = rhi::placement::CreatePlacedImage(*context, imageInfo, rhi::placement::Placement{ heapReply.Get(), imageOffset });
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(imageReply)));
    EXPECT_EQ(imageReply.Description().Type, ResourceType::Image);
    EXPECT_EQ(imageReply.Description().ImageUsage, imageInfo.Usage);
    EXPECT_EQ(imageReply.Description().MipLevels, 2);
    EXPECT_EQ(imageReply.Description().Width, static_cast<int32_t>(k_Size));
    EXPECT_EQ(imageReply.Description().InitialUsage, ImageUsageBits::Invalid);

    // placed resources first, then the heap
    DestroyAndWait(imageReply.Get());
    DestroyAndWait(bufferReply.Get());
    DestroyAndWait(heapReply.Get());
}

TEST_F(ResourceContextTest, DescriptionOfBufferCopyMatchesTheSource)
{
    const std::vector<uint32_t> words = MakeWordPattern(64u, 9u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    BufferCreateInfo info = DescriptionBufferInfo(ResourceDomain::GPUOnly, data.Data.size());
    info.Usage = BufferUsageBits::TransferSource | BufferUsageBits::ComputeShaderSRV;
    const GraphicsResource source = CreateBufferAndWait(info, std::span{ &data, 1u });

    rhi::ResourceFuture<GraphicsResource> reply = context->CopyBuffer(source, true, {});
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(reply)));

    const ResourceDescription& description = reply.Description();
    EXPECT_EQ(description.Type, ResourceType::Buffer);
    EXPECT_EQ(description.Domain, ResourceDomain::GPUOnly);
    EXPECT_EQ(description.ByteSize, data.Data.size());
    EXPECT_NE(description.BufferUsage & BufferUsageBits::TransferDestination, BufferUsageBits::Invalid);
    EXPECT_NE(description.BufferUsage & BufferUsageBits::ComputeShaderSRV, BufferUsageBits::Invalid);

    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(reply.Get());
    DestroyAndWait(source);
}

TEST_F(ResourceContextTest, DescriptionOfImageCopyHasTheCopyUsage)
{
    const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 13u);
    ImageCreateInfo info = DescriptionImageInfo(ImageUsageBits::SampledImage | ImageUsageBits::TransferSource);
    info.MipLevels = 1u;
    const GraphicsResource source = MakeImageWithData(*context, info, texels);
    ASSERT_TRUE(source);

    rhi::ResourceFuture<GraphicsResource> reply = context->CopyImage(source, k_SampledToSampled, ImageUsageBits::SampledImage, {});
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(reply)));

    const ResourceDescription& description = reply.Description();
    EXPECT_EQ(description.Type, ResourceType::Image);
    EXPECT_EQ(description.Dimensions, ImageType::Type2D);
    EXPECT_TRUE(description.Format == info.Format);
    EXPECT_EQ(description.Width, static_cast<int32_t>(k_Size));
    EXPECT_EQ(description.Height, static_cast<int32_t>(k_Size));
    EXPECT_EQ(description.MipLevels, 1);
    EXPECT_EQ(description.ArrayLayers, 1);
    EXPECT_EQ(description.SampleCount, 1);
    EXPECT_NE(description.ImageUsage & ImageUsageBits::TransferDestination, ImageUsageBits::Invalid);
    EXPECT_EQ(description.InitialUsage, ImageUsageBits::SampledImage);

    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(reply.Get());
    DestroyAndWait(source);
}

TEST_F(ResourceContextTest, DescriptionFollowsTheResourceWhenASlotIsReused)
{
    // three live futures at most, so the image takes the slot that the buffer future frees
    RecreateContext(4);

    GraphicsResource buffer = GraphicsResource::Null();
    {
        BufferCreateInfo bufferInfo = DescriptionBufferInfo(ResourceDomain::CPUToGPU, 512u);
        bufferInfo.Usage = BufferUsageBits::ComputeShaderSRV;
        rhi::ResourceFuture<GraphicsResource> bufferReply = context->CreateBuffer(bufferInfo, {});
        ASSERT_EQ(context->Wait(bufferReply, k_TimeoutNs), rhi::FutureState::Complete);
        EXPECT_EQ(bufferReply.Description().Type, ResourceType::Buffer);
        buffer = bufferReply.Get();
    }

    ImageCreateInfo imageInfo = DescriptionImageInfo(ImageUsageBits::SampledImage);
    imageInfo.Width = 8u;
    imageInfo.Height = 8u;
    rhi::ResourceFuture<GraphicsResource> imageReply = context->CreateImage(imageInfo);
    ASSERT_TRUE(IsCreatedOrComplete(PollUntilCreated(imageReply)));

    const ResourceDescription& description = imageReply.Description();
    EXPECT_EQ(description.Type, ResourceType::Image);
    EXPECT_EQ(description.ByteSize, 0u);
    EXPECT_EQ(description.BufferUsage, BufferUsageBits::Invalid);
    EXPECT_EQ(description.Width, 8);
    EXPECT_EQ(description.Height, 8);
    EXPECT_EQ(description.MipLevels, 1);

    ASSERT_EQ(context->Wait(imageReply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(imageReply.Get());
    DestroyAndWait(buffer);
}

TEST_F(ResourceContextTest, FourThreadsReadDescriptionsAtTheFirstCreatedState)
{
    constexpr int32_t k_Threads = 4;
    constexpr int32_t k_BuffersPerThread = 200;
    std::atomic<int32_t> mismatches{ 0 };
    std::atomic<int32_t> failures{ 0 };

    // each thread creates all its buffers first, so that many futures are in flight while it reads
    const auto runThread = [this, &mismatches, &failures](int32_t thread_index)
    {
        std::mt19937 random(static_cast<uint32_t>(1000 + thread_index));
        std::vector<rhi::ResourceFuture<GraphicsResource>> replies;
        std::vector<uint64_t> sizes;

        for (int32_t index = 0; index < k_BuffersPerThread; ++index)
        {
            const size_t size = size_t{ 4u } * (1u + random() % 1024u);
            const std::vector<uint32_t> words = MakeWordPattern(static_cast<uint32_t>(size / 4u), static_cast<uint32_t>(index));
            const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

            // every other buffer takes the transfer path, so both the Created and the Complete state are read
            const bool withData = (index % 2) == 0;
            BufferCreateInfo info = DescriptionBufferInfo(withData ? ResourceDomain::GPUOnly : ResourceDomain::CPUToGPU, size);
            info.Usage = BufferUsageBits::ComputeShaderSRV;

            sizes.push_back(size);
            replies.push_back(withData ? context->CreateBuffer(info, std::span{ &data, 1u }) : context->CreateBuffer(info, {}));
        }

        for (size_t index = 0u; index < replies.size(); ++index)
        {
            const rhi::FutureState firstState = PollUntilCreated(replies[index]);
            if (!IsCreatedOrComplete(firstState))
            {
                ++failures;
                continue;
            }

            if (replies[index].Description().ByteSize != sizes[index])
            {
                ++mismatches;
            }
        }

        for (size_t index = 0u; index < replies.size(); ++index)
        {
            if (context->Wait(replies[index], k_TimeoutNs) != rhi::FutureState::Complete)
            {
                ++failures;
                continue;
            }

            rhi::OperationFuture destroyReply = context->DestroyResource(replies[index].Get());
            if (context->Wait(destroyReply, k_TimeoutNs) != rhi::FutureState::Complete)
            {
                ++failures;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int32_t threadIndex = 0; threadIndex < k_Threads; ++threadIndex)
    {
        threads.emplace_back(runThread, threadIndex);
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(mismatches.load(), 0);
}

namespace
{

    rhi::FutureState PollUntilCreated(const rhi::OperationFuture& future)
    {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point deadline = Clock::now() + std::chrono::nanoseconds(k_PollLimitNs);
        rhi::FutureState state = future.State();

        while (state == rhi::FutureState::Pending && Clock::now() < deadline)
        {
            std::this_thread::yield();
            state = future.State();
        }

        return state;
    }

    ImageCreateInfo DescriptionImageInfo(ImageUsageBits usage)
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

    BufferCreateInfo DescriptionBufferInfo(ResourceDomain domain, size_t size)
    {
        BufferCreateInfo info{};
        info.Domain = domain;
        info.Usage = BufferUsageBits::TransferSource | BufferUsageBits::TransferDestination;
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

    bool IsCreatedOrComplete(rhi::FutureState state) noexcept
    {
        return state == rhi::FutureState::Created || state == rhi::FutureState::Complete;
    }

    GraphicsResource MakeImageWithData(ResourceContext& context, const ImageCreateInfo& info, const std::vector<uint32_t>& texels)
    {
        const RhiImageResourceData data = WholeImageData(texels);
        rhi::ResourceFuture<GraphicsResource> reply = context.CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::SampledImage);
        EXPECT_EQ(context.Wait(reply, k_PollLimitNs), rhi::FutureState::Complete);
        if (reply.State() != rhi::FutureState::Complete)
        {
            return GraphicsResource::Null();
        }

        return reply.Get();
    }

}
