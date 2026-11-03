#include "Device.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
#include "ResourcePlacement.hpp"
#include "ResourceTypes.hpp"
#include "Semaphore.hpp"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <span>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace
{
    constexpr uint32_t k_Size = 16u;
    constexpr size_t k_ImageBytes = size_t{ k_Size } * k_Size * sizeof(uint32_t);
    constexpr uint64_t k_WaitNs = 5'000'000'000ull;
    // long enough that a retirement that should not happen would have happened, short enough to keep the suite fast
    constexpr uint64_t k_ShortWaitNs = 100'000'000ull;

    constexpr ImageUsageBits k_ImageUsage = ImageUsageBits::SampledImage | ImageUsageBits::TransferDestination | ImageUsageBits::TransferSource;

    constexpr rhi::ImageUsageTransition k_DiscardToSampled{ ImageUsageBits::Discard, ImageUsageBits::SampledImage };
    constexpr rhi::ImageUsageTransition k_SampledToSampled{ ImageUsageBits::SampledImage, ImageUsageBits::SampledImage };

    [[nodiscard]] ImageCreateInfo PlacementImageInfo(ImageUsageBits usage);
    [[nodiscard]] BufferCreateInfo PlacementBufferInfo(size_t size);
    [[nodiscard]] RhiImageResourceData WholeImageData(const std::vector<uint32_t>& texels);
    [[nodiscard]] uint64_t AlignUp(uint64_t value, uint64_t alignment) noexcept;
    [[nodiscard]] VkMemoryRequirements RealImageRequirements(VkDevice device, const GraphicsResource& image);
    [[nodiscard]] VkMemoryRequirements RealBufferRequirements(VkDevice device, const GraphicsResource& buffer);
    void ExpectSameRequirements(const rhi::placement::MemoryRequirements& queried, const VkMemoryRequirements& real, const char* name);
    /** @brief Makes a heap, waits for it, and returns it. Returns a null resource, with a failed expectation, when it does not complete. */
    [[nodiscard]] GraphicsResource MakeHeap(ResourceContext& context, uint64_t size, uint32_t memory_type_bits);
    [[nodiscard]] GraphicsResource MakePlacedImage(ResourceContext& context, const ImageCreateInfo& info, const GraphicsResource& heap, uint64_t offset);
    void WaitForDestroy(ResourceContext& context, const GraphicsResource& resource);
}

TEST_F(ResourceContextTest, PlacementQueriesMatchCreatedResources)
{
    struct ImageCase
    {
        const char* Name;
        ImageCreateInfo Info;
    };

    ImageCreateInfo mipped = PlacementImageInfo(k_ImageUsage);
    mipped.Width = 64u;
    mipped.Height = 64u;
    mipped.MipLevels = 7u;

    ImageCreateInfo layered = PlacementImageInfo(k_ImageUsage);
    layered.Width = 32u;
    layered.Height = 32u;
    layered.MipLevels = 3u;
    layered.ArrayLayers = 6u;

    ImageCreateInfo depth = PlacementImageInfo(ImageUsageBits::DepthWrite | ImageUsageBits::SampledImage);
    depth.Format = ImageFormat{ ImageComponentFormats::D32, ImageDataType::Float };
    depth.Width = 128u;
    depth.Height = 128u;

    ImageCreateInfo renderTarget = PlacementImageInfo(ImageUsageBits::RenderTargetWrite | ImageUsageBits::SampledImage);
    renderTarget.Format = ImageFormat{ ImageComponentFormats::RGBA16, ImageDataType::Float };
    renderTarget.Width = 256u;
    renderTarget.Height = 256u;

    // a multisampled image needs far more memory. this device reports the same requirements for every usage, so the
    // sample count is the one field of the create info that the query must carry for these cases to differ
    ImageCreateInfo multisampled = PlacementImageInfo(ImageUsageBits::RenderTargetWrite);
    multisampled.Width = 256u;
    multisampled.Height = 256u;
    multisampled.SampleCount = 4u;

    // a cube view adds a create flag on the image
    ImageCreateInfo cube = PlacementImageInfo(k_ImageUsage);
    cube.Width = 32u;
    cube.Height = 32u;
    cube.ArrayLayers = 6u;
    cube.ViewInfo = ImageViewCreateInfo{};
    cube.ViewInfo->ViewType = ImageViewType::Cube;
    cube.ViewInfo->Range.LayerCount = 6u;

    std::vector<ImageCase> imageCases;
    imageCases.push_back({ "color with mips", mipped });
    imageCases.push_back({ "array with mips", layered });
    imageCases.push_back({ "depth", depth });
    imageCases.push_back({ "render target", renderTarget });
    imageCases.push_back({ "multisampled render target", multisampled });
    imageCases.push_back({ "cube", cube });

    const VkDevice vkDevice = rhiSystem->GetDevice()->Handle().As<VkDevice>();
    for (const ImageCase& entry : imageCases)
    {
        const rhi::placement::MemoryRequirements queried = rhi::placement::GetImageMemoryRequirements(*context, entry.Info);
        EXPECT_GT(queried.Size, 0u) << entry.Name;

        rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(entry.Info);
        ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete) << entry.Name;
        const GraphicsResource image = reply.Get();

        ExpectSameRequirements(queried, RealImageRequirements(vkDevice, image), entry.Name);
        DestroyAndWait(image);
    }

    BufferCreateInfo copyBuffer = PlacementBufferInfo(1000u);
    BufferCreateInfo storageBuffer = PlacementBufferInfo(70'000u);
    storageBuffer.Usage |= BufferUsageBits::ComputeShaderUAV;

    const std::vector<BufferCreateInfo> bufferCases{ copyBuffer, storageBuffer };
    for (const BufferCreateInfo& info : bufferCases)
    {
        const rhi::placement::MemoryRequirements queried = rhi::placement::GetBufferMemoryRequirements(*context, info);
        EXPECT_GT(queried.Size, 0u);

        const GraphicsResource buffer = CreateBufferAndWait(info, {});
        ExpectSameRequirements(queried, RealBufferRequirements(vkDevice, buffer), "buffer");
        DestroyAndWait(buffer);
    }

    // an info that CreateImage refuses has no requirements
    ImageCreateInfo empty = PlacementImageInfo(k_ImageUsage);
    empty.Width = 0u;
    EXPECT_EQ(rhi::placement::GetImageMemoryRequirements(*context, empty).Size, 0u);
    EXPECT_EQ(rhi::placement::GetBufferMemoryRequirements(*context, PlacementBufferInfo(0u)).Size, 0u);
}

TEST_F(ResourceContextTest, PlacementAliasedImagesShareMemory)
{
    const ImageCreateInfo info = PlacementImageInfo(k_ImageUsage);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetImageMemoryRequirements(*context, info);
    const GraphicsResource heap = MakeHeap(*context, requirements.Size, requirements.MemoryTypeBits);
    ASSERT_TRUE(heap);

    const GraphicsResource imageA = MakePlacedImage(*context, info, heap, 0u);
    const GraphicsResource imageB = MakePlacedImage(*context, info, heap, 0u);
    ASSERT_TRUE(imageA);
    ASSERT_TRUE(imageB);

    // the first write of each image after the other used the memory discards the old contents
    const std::vector<uint32_t> texelsA = MakeWordPattern(k_Size * k_Size, 101u);
    const RhiImageResourceData dataA = WholeImageData(texelsA);
    rhi::OperationFuture uploadA = context->SetImageData(imageA, k_DiscardToSampled, std::span{ &dataA, 1u }, {});
    ASSERT_EQ(context->Wait(uploadA, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackImageWords(imageA, k_SampledToSampled, k_ImageBytes), texelsA);

    const std::vector<uint32_t> texelsB = MakeWordPattern(k_Size * k_Size, 102u);
    const RhiImageResourceData dataB = WholeImageData(texelsB);
    rhi::OperationFuture uploadB = context->SetImageData(imageB, k_DiscardToSampled, std::span{ &dataB, 1u }, {});
    ASSERT_EQ(context->Wait(uploadB, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackImageWords(imageB, k_SampledToSampled, k_ImageBytes), texelsB);

    WaitForDestroy(*context, imageA);
    WaitForDestroy(*context, imageB);
    WaitForDestroy(*context, heap);
}

TEST_F(ResourceContextTest, PlacementImagesAtTwoOffsetsDoNotOverlap)
{
    const ImageCreateInfo info = PlacementImageInfo(k_ImageUsage);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetImageMemoryRequirements(*context, info);
    const uint64_t secondOffset = AlignUp(requirements.Size, requirements.Alignment);
    const GraphicsResource heap = MakeHeap(*context, secondOffset + requirements.Size, requirements.MemoryTypeBits);
    ASSERT_TRUE(heap);

    const GraphicsResource imageA = MakePlacedImage(*context, info, heap, 0u);
    const GraphicsResource imageB = MakePlacedImage(*context, info, heap, secondOffset);
    ASSERT_TRUE(imageA);
    ASSERT_TRUE(imageB);

    const std::vector<uint32_t> texelsA = MakeWordPattern(k_Size * k_Size, 111u);
    const std::vector<uint32_t> texelsB = MakeWordPattern(k_Size * k_Size, 112u);
    const RhiImageResourceData dataA = WholeImageData(texelsA);
    const RhiImageResourceData dataB = WholeImageData(texelsB);

    rhi::OperationFuture uploadA = context->SetImageData(imageA, k_DiscardToSampled, std::span{ &dataA, 1u }, {});
    rhi::OperationFuture uploadB = context->SetImageData(imageB, k_DiscardToSampled, std::span{ &dataB, 1u }, {});
    ASSERT_EQ(context->Wait(uploadA, k_TimeoutNs), rhi::FutureState::Complete);
    ASSERT_EQ(context->Wait(uploadB, k_TimeoutNs), rhi::FutureState::Complete);

    EXPECT_EQ(ReadBackImageWords(imageA, k_SampledToSampled, k_ImageBytes), texelsA);
    EXPECT_EQ(ReadBackImageWords(imageB, k_SampledToSampled, k_ImageBytes), texelsB);

    WaitForDestroy(*context, imageA);
    WaitForDestroy(*context, imageB);
    WaitForDestroy(*context, heap);
}

TEST_F(ResourceContextTest, PlacementBufferRoundTrip)
{
    constexpr size_t k_BufferBytes = 1024u;
    const BufferCreateInfo info = PlacementBufferInfo(k_BufferBytes);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetBufferMemoryRequirements(*context, info);
    const GraphicsResource heap = MakeHeap(*context, requirements.Size, requirements.MemoryTypeBits);
    ASSERT_TRUE(heap);

    rhi::ResourceFuture<GraphicsResource> reply = rhi::placement::CreatePlacedBuffer(*context, info, rhi::placement::Placement{ heap, 0u });
    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource buffer = reply.Get();
    EXPECT_EQ(buffer.Type, ResourceType::Buffer);

    rhi::OperationFuture fill = context->FillBuffer(buffer, 0xDEADBEEFu, 0u, k_BufferBytes, {});
    ASSERT_EQ(context->Wait(fill, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackWords(buffer, k_BufferBytes), std::vector<uint32_t>(k_BufferBytes / sizeof(uint32_t), 0xDEADBEEFu));

    const std::vector<uint32_t> words = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 121u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    rhi::OperationFuture upload = context->SetBufferData(buffer, std::span{ &data, 1u }, {});
    ASSERT_EQ(context->Wait(upload, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackWords(buffer, k_BufferBytes), words);

    // a placed buffer is device-local and owns no allocation, so the host calls refuse it
    rhi::ResourceFuture<void*> map = context->MapBuffer(buffer, 16u, 0u);
    EXPECT_EQ(context->Wait(map, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(map.Error(), rhi::ResourceError::NotHostVisible);
    rhi::OperationFuture unmap = context->UnmapBuffer(buffer, 16u, 0u);
    EXPECT_EQ(context->Wait(unmap, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(unmap.Error(), rhi::ResourceError::NotHostVisible);

    WaitForDestroy(*context, buffer);
    WaitForDestroy(*context, heap);
}

TEST_F(ResourceContextTest, PlacementRefusals)
{
    const ImageCreateInfo info = PlacementImageInfo(k_ImageUsage);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetImageMemoryRequirements(*context, info);
    ASSERT_GT(requirements.Alignment, 1u);

    const GraphicsResource heap = MakeHeap(*context, requirements.Size, requirements.MemoryTypeBits);
    ASSERT_TRUE(heap);

    // room for the image at its alignment and a little more, so that a misaligned offset fails only the alignment check
    const GraphicsResource roomyHeap = MakeHeap(*context, requirements.Size + requirements.Alignment * 2u, requirements.MemoryTypeBits);
    ASSERT_TRUE(roomyHeap);

    const GraphicsResource notAHeap = CreateBufferAndWait(PlacementBufferInfo(256u), {});

    ImageCreateInfo hostInfo = info;
    hostInfo.Domain = ResourceDomain::CPUToGPU;
    ImageCreateInfo discardInfo = info;
    discardInfo.Usage |= ImageUsageBits::Discard;

    // a heap that is destroyed, so that its GraphicsResource is stale
    const GraphicsResource staleHeap = MakeHeap(*context, requirements.Size, requirements.MemoryTypeBits);
    ASSERT_TRUE(staleHeap);
    WaitForDestroy(*context, staleHeap);

    struct Case
    {
        const char* Name;
        rhi::ResourceFuture<GraphicsResource> Future;
        rhi::ResourceError Expected;
    };

    using rhi::placement::Placement;
    std::vector<Case> cases;
    cases.push_back({ "misaligned offset", rhi::placement::CreatePlacedImage(*context, info, Placement{ roomyHeap, 1u }), rhi::ResourceError::InvalidPlacement });
    cases.push_back({ "misaligned offset past the first alignment", rhi::placement::CreatePlacedImage(*context, info, Placement{ roomyHeap, requirements.Alignment + 4u }), rhi::ResourceError::InvalidPlacement });
    cases.push_back({ "past the heap end", rhi::placement::CreatePlacedImage(*context, info, Placement{ heap, requirements.Alignment }), rhi::ResourceError::InvalidPlacement });
    cases.push_back({ "offset far past the heap", rhi::placement::CreatePlacedImage(*context, info, Placement{ heap, requirements.Alignment * 1'000'000ull }), rhi::ResourceError::InvalidPlacement });
    cases.push_back({ "offset that overflows the end", rhi::placement::CreatePlacedImage(*context, info, Placement{ heap, ~0ull - (requirements.Alignment - 1u) }), rhi::ResourceError::InvalidPlacement });
    cases.push_back({ "a buffer as the heap", rhi::placement::CreatePlacedImage(*context, info, Placement{ notAHeap, 0u }), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "a destroyed heap", rhi::placement::CreatePlacedImage(*context, info, Placement{ staleHeap, 0u }), rhi::ResourceError::StaleResource });
    cases.push_back({ "a host-visible domain", rhi::placement::CreatePlacedImage(*context, hostInfo, Placement{ heap, 0u }), rhi::ResourceError::InvalidCreateInfo });
    cases.push_back({ "a discard in the usage", rhi::placement::CreatePlacedImage(*context, discardInfo, Placement{ heap, 0u }), rhi::ResourceError::InvalidCreateInfo });
    cases.push_back({ "a buffer past the heap end", rhi::placement::CreatePlacedBuffer(*context, PlacementBufferInfo(requirements.Size * 2u), Placement{ heap, 0u }), rhi::ResourceError::InvalidPlacement });
    cases.push_back({ "a buffer misaligned", rhi::placement::CreatePlacedBuffer(*context, PlacementBufferInfo(16u), Placement{ heap, 1u }), rhi::ResourceError::InvalidPlacement });
    cases.push_back({ "a buffer in a non-heap", rhi::placement::CreatePlacedBuffer(*context, PlacementBufferInfo(16u), Placement{ notAHeap, 0u }), rhi::ResourceError::WrongResourceKind });
    cases.push_back({ "heap size of zero", rhi::placement::CreateMemoryHeap(*context, rhi::placement::MemoryHeapCreateInfo{ 0u, requirements.MemoryTypeBits, {} }), rhi::ResourceError::InvalidCreateInfo });
    cases.push_back({ "heap memory types of zero", rhi::placement::CreateMemoryHeap(*context, rhi::placement::MemoryHeapCreateInfo{ requirements.Size, 0u, {} }), rhi::ResourceError::InvalidCreateInfo });

    for (const Case& entry : cases)
    {
        EXPECT_EQ(context->Wait(entry.Future, k_TimeoutNs), rhi::FutureState::Failed) << entry.Name;
        EXPECT_EQ(entry.Future.Error(), entry.Expected)
            << entry.Name << ": got " << rhi::ToString(entry.Future.Error()) << ", expected " << rhi::ToString(entry.Expected);
    }

    cases.clear();

    // the refused calls left both heaps without placements, so both destroys complete
    WaitForDestroy(*context, notAHeap);
    WaitForDestroy(*context, roomyHeap);
    WaitForDestroy(*context, heap);
}

TEST_F(ResourceContextTest, PlacementRefusesAMemoryTypeTheImageLacks)
{
    const ImageCreateInfo info = PlacementImageInfo(k_ImageUsage);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetImageMemoryRequirements(*context, info);

    // look for a device-local memory type that this image does not allow
    for (int32_t typeIndex = 0; typeIndex < 32; ++typeIndex)
    {
        const uint32_t typeBit = 1u << typeIndex;
        if ((requirements.MemoryTypeBits & typeBit) != 0u)
        {
            continue;
        }

        rhi::ResourceFuture<GraphicsResource> heapReply = rhi::placement::CreateMemoryHeap(*context, rhi::placement::MemoryHeapCreateInfo{ requirements.Size, typeBit, {} });
        if (context->Wait(heapReply, k_TimeoutNs) != rhi::FutureState::Complete)
        {
            continue;
        }

        const GraphicsResource heap = heapReply.Get();
        rhi::ResourceFuture<GraphicsResource> placed = rhi::placement::CreatePlacedImage(*context, info, rhi::placement::Placement{ heap, 0u });
        EXPECT_EQ(context->Wait(placed, k_TimeoutNs), rhi::FutureState::Failed);
        EXPECT_EQ(placed.Error(), rhi::ResourceError::InvalidPlacement);

        WaitForDestroy(*context, heap);
        return;
    }

    GTEST_SKIP() << "every device-local memory type of this device allows the image";
}

TEST_F(ResourceContextTest, PlacementHeapIsInUseUntilItsResourcesAreFreed)
{
    const ImageCreateInfo info = PlacementImageInfo(k_ImageUsage);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetImageMemoryRequirements(*context, info);
    const GraphicsResource heap = MakeHeap(*context, requirements.Size, requirements.MemoryTypeBits);
    ASSERT_TRUE(heap);

    const GraphicsResource first = MakePlacedImage(*context, info, heap, 0u);
    const GraphicsResource second = MakePlacedImage(*context, info, heap, 0u);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    // two placements are alive, and each destroy of the heap is refused. the heap stays usable
    for (int32_t attempt = 0; attempt < 2; ++attempt)
    {
        rhi::OperationFuture refused = context->DestroyResource(heap);
        EXPECT_EQ(context->Wait(refused, k_TimeoutNs), rhi::FutureState::Failed);
        EXPECT_EQ(refused.Error(), rhi::ResourceError::HeapInUse);

        if (attempt == 0)
        {
            WaitForDestroy(*context, first);
        }
    }

    // one placement is left, so a new image fits in the heap
    const GraphicsResource third = MakePlacedImage(*context, info, heap, 0u);
    ASSERT_TRUE(third);

    WaitForDestroy(*context, second);
    WaitForDestroy(*context, third);
    WaitForDestroy(*context, heap);
}

TEST_F(ResourceContextTest, PlacementDestroyWithATransferInFlight)
{
    const int64_t futuresAtStart = context->Counters().FuturesLive;

    {
        const ImageCreateInfo info = PlacementImageInfo(k_ImageUsage);
        const rhi::placement::MemoryRequirements requirements = rhi::placement::GetImageMemoryRequirements(*context, info);
        const GraphicsResource heap = MakeHeap(*context, requirements.Size, requirements.MemoryTypeBits);
        ASSERT_TRUE(heap);

        const GraphicsResource image = MakePlacedImage(*context, info, heap, 0u);
        ASSERT_TRUE(image);

        // the upload is not waited for, so the destroy has to wait for the transfer
        const std::vector<uint32_t> texels = MakeWordPattern(k_Size * k_Size, 131u);
        const RhiImageResourceData data = WholeImageData(texels);
        rhi::OperationFuture upload = context->SetImageData(image, k_DiscardToSampled, std::span{ &data, 1u }, {});
        rhi::OperationFuture destroyImage = context->DestroyResource(image);
        EXPECT_EQ(context->Wait(destroyImage, k_TimeoutNs), rhi::FutureState::Complete);
        EXPECT_EQ(context->Wait(upload, k_TimeoutNs), rhi::FutureState::Complete);

        rhi::OperationFuture destroyHeap = context->DestroyResource(heap);
        EXPECT_EQ(context->Wait(destroyHeap, k_TimeoutNs), rhi::FutureState::Complete);
    }

    EXPECT_EQ(context->Counters().FuturesLive, futuresAtStart);
}

TEST_F(ResourceContextTest, PlacementHeapStaysInUseWhileThePlacedResourceRetires)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    ASSERT_EQ(context->Wait(context->RegisterConsumerTimeline(timeline.Handle()), k_TimeoutNs), rhi::FutureState::Complete);

    const ImageCreateInfo info = PlacementImageInfo(k_ImageUsage);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetImageMemoryRequirements(*context, info);
    const GraphicsResource heap = MakeHeap(*context, requirements.Size, requirements.MemoryTypeBits);
    ASSERT_TRUE(heap);

    const GraphicsResource image = MakePlacedImage(*context, info, heap, 0u);
    ASSERT_TRUE(image);

    // the owner is recording the frame that signals 5, and that frame can use the image
    const ConsumerTimelineValue pending{ timeline.Handle(), 5u };
    context->Tick(std::span{ &pending, 1u });

    rhi::OperationFuture destroyImage = context->DestroyResource(image);
    EXPECT_EQ(context->Wait(destroyImage, k_ShortWaitNs), rhi::FutureState::Pending);

    // the image is requested for destroy, and not freed. the GPU can still use its memory
    rhi::OperationFuture refused = context->DestroyResource(heap);
    EXPECT_EQ(context->Wait(refused, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(refused.Error(), rhi::ResourceError::HeapInUse);

    ASSERT_TRUE(timeline.SetValue(5u).IsSuccess());
    context->Tick(std::span{ &pending, 1u });
    EXPECT_EQ(context->Wait(destroyImage, k_TimeoutNs), rhi::FutureState::Complete);

    WaitForDestroy(*context, heap);
    EXPECT_EQ(context->Wait(context->UnregisterConsumerTimeline(timeline.Handle()), k_TimeoutNs), rhi::FutureState::Complete);
}

TEST_F(ResourceContextTest, PlacementShutdownFreesPlacedResourcesBeforeTheirHeap)
{
    const ImageCreateInfo info = PlacementImageInfo(k_ImageUsage);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetImageMemoryRequirements(*context, info);
    const GraphicsResource heap = MakeHeap(*context, requirements.Size, requirements.MemoryTypeBits);
    ASSERT_TRUE(heap);

    const GraphicsResource image = MakePlacedImage(*context, info, heap, 0u);
    EXPECT_TRUE(image);

    // the test ends with the heap and the image alive. TearDown destroys the context, which must free the image first
}

namespace
{
    ImageCreateInfo PlacementImageInfo(ImageUsageBits usage)
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

    BufferCreateInfo PlacementBufferInfo(size_t size)
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

    uint64_t AlignUp(uint64_t value, uint64_t alignment) noexcept
    {
        return ((value + alignment - 1u) / alignment) * alignment;
    }

    VkMemoryRequirements RealImageRequirements(VkDevice device, const GraphicsResource& image)
    {
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, reinterpret_cast<VkImage>(image.VkHandle), &requirements);
        return requirements;
    }

    VkMemoryRequirements RealBufferRequirements(VkDevice device, const GraphicsResource& buffer)
    {
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, reinterpret_cast<VkBuffer>(buffer.VkHandle), &requirements);
        return requirements;
    }

    void ExpectSameRequirements(const rhi::placement::MemoryRequirements& queried, const VkMemoryRequirements& real, const char* name)
    {
        EXPECT_EQ(queried.Size, real.size) << name;
        EXPECT_EQ(queried.Alignment, real.alignment) << name;
        EXPECT_EQ(queried.MemoryTypeBits, real.memoryTypeBits) << name;
    }

    GraphicsResource MakeHeap(ResourceContext& context, uint64_t size, uint32_t memory_type_bits)
    {
        rhi::ResourceFuture<GraphicsResource> reply = rhi::placement::CreateMemoryHeap(context, rhi::placement::MemoryHeapCreateInfo{ size, memory_type_bits, "test_heap" });
        EXPECT_EQ(context.Wait(reply, k_WaitNs), rhi::FutureState::Complete);
        if (reply.State() != rhi::FutureState::Complete)
        {
            return GraphicsResource::Null();
        }

        EXPECT_EQ(reply.Get().Type, ResourceType::Heap);
        return reply.Get();
    }

    GraphicsResource MakePlacedImage(ResourceContext& context, const ImageCreateInfo& info, const GraphicsResource& heap, uint64_t offset)
    {
        rhi::ResourceFuture<GraphicsResource> reply = rhi::placement::CreatePlacedImage(context, info, rhi::placement::Placement{ heap, offset });
        EXPECT_EQ(context.Wait(reply, k_WaitNs), rhi::FutureState::Complete);
        if (reply.State() != rhi::FutureState::Complete)
        {
            return GraphicsResource::Null();
        }

        return reply.Get();
    }

    void WaitForDestroy(ResourceContext& context, const GraphicsResource& resource)
    {
        rhi::OperationFuture reply = context.DestroyResource(resource);
        EXPECT_EQ(context.Wait(reply, k_WaitNs), rhi::FutureState::Complete);
    }
}
