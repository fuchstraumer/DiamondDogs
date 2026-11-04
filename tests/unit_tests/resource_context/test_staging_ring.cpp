#include "ResourceContextFixture.hpp"
#include "Device.hpp"
#include "Semaphore.hpp"
#include <vulkan/vulkan_core.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <print>
#include <random>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    constexpr uint64_t k_SmallRingSize = 1024ull * 1024ull;
    constexpr uint64_t k_UploadTimeoutNs = 20'000'000'000ull;

    /** An upload that the test checks later: the future of its create call, and the words that it must contain. */
    struct StagedItem
    {
        rhi::ResourceFuture<GraphicsResource> Future;
        GraphicsResource Resource{};
        bool IsImage{ false };
        rhi::ImageUsageTransition ReadbackUsage{};
        std::vector<uint32_t> Words;
    };

    /** A texel format with a block size that is not a power of two, and the bytes of one texel. */
    struct OddTexelFormat
    {
        ImageFormat Format;
        VkFormat VulkanFormat;
        uint32_t TexelBytes;
    };

    /** Signals both timelines when the test ends, and then waits for the futures that it tracks. A failed ASSERT returns
     *  from the test early, and a transfer that still waits on a timeline would hang the shutdown of the context. Declare
     *  it after the timelines and the futures, so that it is destroyed before them. */
    class TimelineReleaseGuard
    {
    public:
        TimelineReleaseGuard(ResourceContext& _context, rhi::TimelineSemaphore& first_timeline, rhi::TimelineSemaphore& second_timeline);
        ~TimelineReleaseGuard();
        TimelineReleaseGuard(const TimelineReleaseGuard&) = delete;
        TimelineReleaseGuard& operator=(const TimelineReleaseGuard&) = delete;

        void Track(rhi::OperationFuture& future);

    private:
        /** Sets the timeline to 1 unless the test did already. */
        static void releaseIfHeld(rhi::TimelineSemaphore& timeline);

        ResourceContext& context;
        rhi::TimelineSemaphore& firstTimeline;
        rhi::TimelineSemaphore& secondTimeline;
        std::vector<rhi::OperationFuture*> tracked;
    };

    [[nodiscard]] BufferCreateInfo DeviceLocalBuffer(size_t size_bytes);

    /** Each Start function sends the create call and returns at once, so that many uploads are in flight together. */
    [[nodiscard]] StagedItem StartBufferUpload(ResourceContext& context, std::vector<uint32_t> words);
    [[nodiscard]] StagedItem StartRgba8Upload(ResourceContext& context, uint32_t width, uint32_t height, uint32_t seed);
    [[nodiscard]] StagedItem StartOddTexelUpload(ResourceContext& context, const OddTexelFormat& format, uint32_t width, uint32_t height, uint32_t seed);
    /** Waits for every upload, keeps the resources and drops the futures. Returns the number of uploads that failed. */
    [[nodiscard]] int32_t FinishUploads(ResourceContext& context, std::span<StagedItem> items);
    /** Returns true when optimal tiling supports the format as a transfer source and a transfer destination. */
    [[nodiscard]] bool FormatCanBeUploadedAndReadBack(VkPhysicalDevice physical_device, VkFormat format);
    [[nodiscard]] bool WaitUntilRingIsFree(const ResourceContext& context);
}

class StagingRingTest : public ResourceContextTest
{
protected:
    /** Reads every item back and returns how many differ from the words that were uploaded. */
    [[nodiscard]] int32_t CountMismatches(const std::vector<StagedItem>& items);
};

// four threads push about thirty times the ring through it with no wait between uploads, and an upload can take over half
// of the ring, so the ring must wrap, stall and free
TEST_F(StagingRingTest, ManyThreadsUploadBuffersAndImagesThroughASmallRing)
{
    constexpr int32_t k_Threads = 4;
    constexpr int32_t k_UploadsPerThread = 40;

    RecreateContextWithRing(k_DefaultMaxFutures, k_SmallRingSize);
    const ResourceContextCounters before = context->Counters();

    std::array<std::vector<StagedItem>, k_Threads> results;
    std::atomic<int32_t> failedUploads{ 0 };

    auto uploadWorker = [this, &results, &failedUploads](int32_t thread_index)
    {
        std::mt19937 random(static_cast<uint32_t>(thread_index) + 100u);
        std::uniform_int_distribution<uint32_t> smallWords(1u, 64u);
        std::uniform_int_distribution<uint32_t> bigWords(1u, 160'000u);
        std::uniform_int_distribution<uint32_t> side(1u, 400u);
        std::uniform_int_distribution<int32_t> percent(0, 99);

        std::vector<StagedItem>& items = results[static_cast<size_t>(thread_index)];
        for (int32_t i = 0; i < k_UploadsPerThread; ++i)
        {
            const uint32_t seed = static_cast<uint32_t>(thread_index * 1000 + i);
            if (i % 2 == 0)
            {
                const uint32_t wordCount = percent(random) < 50 ? smallWords(random) : bigWords(random);
                items.push_back(StartBufferUpload(*context, MakeWordPattern(wordCount, seed)));
            }
            else
            {
                items.push_back(StartRgba8Upload(*context, side(random), side(random), seed));
            }
        }

        failedUploads.fetch_add(FinishUploads(*context, items));
    };

    std::vector<std::thread> threads;
    for (int32_t t = 0; t < k_Threads; ++t)
    {
        threads.emplace_back(uploadWorker, t);
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(failedUploads.load(), 0);

    int32_t mismatches = 0;
    int32_t checked = 0;
    for (const std::vector<StagedItem>& items : results)
    {
        mismatches += CountMismatches(items);
        checked += static_cast<int32_t>(items.size());
    }

    EXPECT_EQ(checked, k_Threads * k_UploadsPerThread);
    EXPECT_EQ(mismatches, 0);

    const ResourceContextCounters after = context->Counters();
    std::println("staging ring: stalls={} dedicated={} bytes staged={}", after.StagingRingStalls, after.StagingDedicatedUploads, after.BytesStaged - before.BytesStaged);
    EXPECT_GT(after.StagingRingStalls, 0);
    EXPECT_EQ(after.StagingDedicatedUploads - before.StagingDedicatedUploads, 0);
    EXPECT_EQ(after.FuturesLive, before.FuturesLive);
    EXPECT_TRUE(WaitUntilRingIsFree(*context));
}

TEST_F(StagingRingTest, AnUploadLargerThanTheRingGetsADedicatedBuffer)
{
    RecreateContextWithRing(k_DefaultMaxFutures, k_SmallRingSize);
    const ResourceContextCounters before = context->Counters();

    // 2 MiB of buffer, and a 1024 x 1024 RGBA8 image of 4 MiB, with a small upload between them
    std::vector<StagedItem> items;
    items.push_back(StartBufferUpload(*context, MakeWordPattern(512u * 1024u, 7u)));
    items.push_back(StartRgba8Upload(*context, 1024u, 1024u, 8u));
    items.push_back(StartBufferUpload(*context, MakeWordPattern(300u, 9u)));

    EXPECT_EQ(FinishUploads(*context, items), 0);
    EXPECT_EQ(CountMismatches(items), 0);

    const ResourceContextCounters after = context->Counters();
    EXPECT_EQ(after.StagingDedicatedUploads - before.StagingDedicatedUploads, 2);
    EXPECT_EQ(after.FuturesLive, before.FuturesLive);
    EXPECT_TRUE(WaitUntilRingIsFree(*context));
}

// the texel of RGB32F is 12 bytes, so a ring offset must be a multiple of 12. buffers of other sizes go between the
// images, so that the tail of the ring is rarely a multiple of 12 when an image comes
TEST_F(StagingRingTest, ImagesWithATexelThatIsNotAPowerOfTwoLandOnAlignedOffsets)
{
    const std::array<OddTexelFormat, 3> candidates
    {
        OddTexelFormat{ ImageFormat{ ImageComponentFormats::RGB32, ImageDataType::Float }, VK_FORMAT_R32G32B32_SFLOAT, 12u },
        OddTexelFormat{ ImageFormat{ ImageComponentFormats::RGB16, ImageDataType::Float }, VK_FORMAT_R16G16B16_SFLOAT, 6u },
        OddTexelFormat{ ImageFormat{ ImageComponentFormats::RGB8, ImageDataType::UNorm }, VK_FORMAT_R8G8B8_UNORM, 3u },
    };

    RecreateContextWithRing(k_DefaultMaxFutures, k_SmallRingSize);
    const VkPhysicalDevice physicalDevice = rhiSystem->GetDevice()->GetPhysicalDevice().As<VkPhysicalDevice>();

    int32_t formatsTested = 0;
    for (const OddTexelFormat& candidate : candidates)
    {
        if (!FormatCanBeUploadedAndReadBack(physicalDevice, candidate.VulkanFormat))
        {
            std::println("staging ring: VkFormat {} cannot be a transfer image on this device, skipped", static_cast<int32_t>(candidate.VulkanFormat));
            continue;
        }

        formatsTested += 1;
        std::mt19937 random(candidate.TexelBytes);

        // a width that is a multiple of 4 keeps the bytes of the image a multiple of 4, which the word readback needs
        std::uniform_int_distribution<uint32_t> quadWidth(1u, 10u);
        std::uniform_int_distribution<uint32_t> height(1u, 40u);
        std::uniform_int_distribution<uint32_t> bufferWords(1u, 300u);

        std::vector<StagedItem> items;
        for (uint32_t i = 0u; i < 48u; ++i)
        {
            items.push_back(StartBufferUpload(*context, MakeWordPattern(bufferWords(random), i)));
            items.push_back(StartOddTexelUpload(*context, candidate, quadWidth(random) * 4u, height(random), i));
        }

        EXPECT_EQ(FinishUploads(*context, items), 0);
        EXPECT_EQ(CountMismatches(items), 0) << "texel bytes " << candidate.TexelBytes;
    }

    if (formatsTested == 0)
    {
        GTEST_SKIP() << "no 12-byte, 6-byte or 3-byte texel format can be a transfer image on this device";
    }
}

// two held uploads sit at the front of the ring, and the GPU cannot finish them until the test signals a timeline. a
// third upload fills the rest of the ring, and a fourth is larger than the space that the first one frees. it must wait
// for the second one. if the ring freed a range before its ticket completed, the fourth upload would overwrite the
// staged bytes of the second one, which the GPU has not read yet, and the second buffer would hold the wrong words
TEST_F(StagingRingTest, ARangeIsNotReusedBeforeItsTransferFinishes)
{
    constexpr uint32_t k_HeldWords = 65536u;
    constexpr uint32_t k_FillerWords = 131072u;
    constexpr uint32_t k_LateWords = 98304u;
    constexpr std::chrono::milliseconds k_SettleTime{ 150 };

    RecreateContextWithRing(k_DefaultMaxFutures, k_SmallRingSize);
    const ResourceContextCounters before = context->Counters();

    const GraphicsResource firstBuffer = CreateBufferAndWait(DeviceLocalBuffer(k_HeldWords * sizeof(uint32_t)), {});
    const GraphicsResource secondBuffer = CreateBufferAndWait(DeviceLocalBuffer(k_HeldWords * sizeof(uint32_t)), {});
    const GraphicsResource fillerBuffer = CreateBufferAndWait(DeviceLocalBuffer(k_FillerWords * sizeof(uint32_t)), {});
    const GraphicsResource lateBuffer = CreateBufferAndWait(DeviceLocalBuffer(k_LateWords * sizeof(uint32_t)), {});

    const std::vector<uint32_t> firstWords = MakeWordPattern(k_HeldWords, 1u);
    const std::vector<uint32_t> secondWords = MakeWordPattern(k_HeldWords, 2u);
    const std::vector<uint32_t> fillerWords = MakeWordPattern(k_FillerWords, 3u);
    const std::vector<uint32_t> lateWords = MakeWordPattern(k_LateWords, 4u);
    const RhiBufferResourceData firstData{ std::as_bytes(std::span{ firstWords }), 0u };
    const RhiBufferResourceData secondData{ std::as_bytes(std::span{ secondWords }), 0u };
    const RhiBufferResourceData fillerData{ std::as_bytes(std::span{ fillerWords }), 0u };
    const RhiBufferResourceData lateData{ std::as_bytes(std::span{ lateWords }), 0u };

    rhi::TimelineSemaphore firstTimeline(rhiSystem->GetDevice()->Handle());
    rhi::TimelineSemaphore secondTimeline(rhiSystem->GetDevice()->Handle());
    rhi::OperationFuture firstUpload;
    rhi::OperationFuture secondUpload;
    rhi::OperationFuture fillerUpload;
    rhi::OperationFuture lateUpload;
    TimelineReleaseGuard guard(*context, firstTimeline, secondTimeline);
    guard.Track(firstUpload);
    guard.Track(secondUpload);
    guard.Track(fillerUpload);
    guard.Track(lateUpload);

    const rhi::TransferWait firstWait{ firstTimeline.Handle(), 1u };
    const rhi::TransferWait secondWait{ secondTimeline.Handle(), 1u };
    firstUpload = context->SetBufferData(firstBuffer, std::span{ &firstData, 1u }, std::span{ &firstWait, 1u });
    secondUpload = context->SetBufferData(secondBuffer, std::span{ &secondData, 1u }, std::span{ &secondWait, 1u });
    fillerUpload = context->SetBufferData(fillerBuffer, std::span{ &fillerData, 1u }, {});
    lateUpload = context->SetBufferData(lateBuffer, std::span{ &lateData, 1u }, {});

    // the transfer thread is now blocked in the ring: the late upload cannot get a range
    std::this_thread::sleep_for(k_SettleTime);
    EXPECT_NE(lateUpload.State(), rhi::FutureState::Complete);

    // the first range is free after this. the late upload needs more than that, so it still waits for the second one
    ASSERT_TRUE(firstTimeline.SetValue(1u).IsSuccess());
    EXPECT_EQ(context->Wait(firstUpload, k_TimeoutNs), rhi::FutureState::Complete);
    std::this_thread::sleep_for(k_SettleTime);
    EXPECT_NE(lateUpload.State(), rhi::FutureState::Complete);

    ASSERT_TRUE(secondTimeline.SetValue(1u).IsSuccess());
    EXPECT_EQ(context->Wait(secondUpload, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(context->Wait(fillerUpload, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(context->Wait(lateUpload, k_TimeoutNs), rhi::FutureState::Complete);

    EXPECT_EQ(ReadBackWords(firstBuffer, firstWords.size() * sizeof(uint32_t)), firstWords);
    EXPECT_EQ(ReadBackWords(secondBuffer, secondWords.size() * sizeof(uint32_t)), secondWords);
    EXPECT_EQ(ReadBackWords(fillerBuffer, fillerWords.size() * sizeof(uint32_t)), fillerWords);
    EXPECT_EQ(ReadBackWords(lateBuffer, lateWords.size() * sizeof(uint32_t)), lateWords);

    const ResourceContextCounters after = context->Counters();
    EXPECT_GE(after.StagingRingStalls - before.StagingRingStalls, 1);
}

TEST_F(StagingRingTest, ARingBelowTheMinimumIsRefused)
{
    ResourceContext tooSmall;
    const ResourceContextCreateInfo createInfo
    {
        rhiSystem->GetInstance(),
        rhiSystem->GetDevice(),
        rhiSystem->GetVulkanApiVersion(),
        true,
        false,
        k_DefaultMaxFutures,
        k_MinStagingRingSize - 1u
    };

    // the check runs before any device object exists, so the context of the fixture does not matter
    EXPECT_EQ(tooSmall.Initialize(createInfo), ResourceContextError::InvalidCreateInfo);
}

int32_t StagingRingTest::CountMismatches(const std::vector<StagedItem>& items)
{
    int32_t mismatches = 0;

    for (const StagedItem& item : items)
    {
        const size_t sizeBytes = item.Words.size() * sizeof(uint32_t);
        const std::vector<uint32_t> words = item.IsImage
            ? ReadBackImageWords(item.Resource, item.ReadbackUsage, sizeBytes)
            : ReadBackWords(item.Resource, sizeBytes);

        if (words != item.Words)
        {
            mismatches += 1;
        }
    }

    return mismatches;
}

namespace
{
    TimelineReleaseGuard::TimelineReleaseGuard(ResourceContext& _context, rhi::TimelineSemaphore& first_timeline, rhi::TimelineSemaphore& second_timeline) :
      context{ _context },
      firstTimeline{ first_timeline },
      secondTimeline{ second_timeline }
    {}

    TimelineReleaseGuard::~TimelineReleaseGuard()
    {
        releaseIfHeld(firstTimeline);
        releaseIfHeld(secondTimeline);

        for (rhi::OperationFuture* future : tracked)
        {
            [[maybe_unused]] const rhi::FutureState state = context.Wait(*future, k_UploadTimeoutNs);
        }
    }

    void TimelineReleaseGuard::releaseIfHeld(rhi::TimelineSemaphore& timeline)
    {
        uint64_t value = 0u;
        if (timeline.GetValue(value).IsSuccess() && value < 1u)
        {
            [[maybe_unused]] const rhi::Result result = timeline.SetValue(1u);
        }
    }

    void TimelineReleaseGuard::Track(rhi::OperationFuture& future)
    {
        tracked.push_back(&future);
    }

    BufferCreateInfo DeviceLocalBuffer(size_t size_bytes)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderSRV | BufferUsageBits::CopySource | BufferUsageBits::CopyDestination;
        info.Size = size_bytes;
        return info;
    }

    StagedItem StartBufferUpload(ResourceContext& context, std::vector<uint32_t> words)
    {
        const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderSRV | BufferUsageBits::CopySource;
        info.Size = data.Data.size();

        StagedItem item;
        item.Future = context.CreateBuffer(info, std::span{ &data, 1u });
        item.IsImage = false;
        item.Words = std::move(words);
        return item;
    }

    StagedItem StartRgba8Upload(ResourceContext& context, uint32_t width, uint32_t height, uint32_t seed)
    {
        std::vector<uint32_t> texels = MakeWordPattern(width * height, seed);
        RhiImageResourceData data{};
        data.Data = std::as_bytes(std::span{ texels });
        data.Width = width;
        data.Height = height;

        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::SrvComputeShader | ImageUsageBits::TransferSource;
        info.Type = ImageType::Type2D;
        info.Format = ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
        info.Width = width;
        info.Height = height;
        info.Depth = 1u;

        StagedItem item;
        item.Future = context.CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::SampledImage);
        item.IsImage = true;
        item.ReadbackUsage = rhi::ImageUsageTransition{ ImageUsageBits::SampledImage, ImageUsageBits::SampledImage };
        item.Words = std::move(texels);
        return item;
    }

    StagedItem StartOddTexelUpload(ResourceContext& context, const OddTexelFormat& format, uint32_t width, uint32_t height, uint32_t seed)
    {
        // the width is a multiple of 4, so the bytes of the image are whole words
        const uint32_t imageWords = width * height * format.TexelBytes / 4u;
        std::vector<uint32_t> words = MakeWordPattern(imageWords, seed);

        RhiImageResourceData data{};
        data.Data = std::as_bytes(std::span{ words });
        data.Width = width;
        data.Height = height;

        // transfer usage only: these formats are often not sampleable
        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::TransferSource;
        info.Type = ImageType::Type2D;
        info.Format = format.Format;
        info.Width = width;
        info.Height = height;
        info.Depth = 1u;

        StagedItem item;
        item.Future = context.CreateImage(info, std::span{ &data, 1u }, ImageUsageBits::TransferSource);
        item.IsImage = true;
        item.ReadbackUsage = rhi::ImageUsageTransition{ ImageUsageBits::TransferSource, ImageUsageBits::TransferSource };
        item.Words = std::move(words);
        return item;
    }

    int32_t FinishUploads(ResourceContext& context, std::span<StagedItem> items)
    {
        int32_t failures = 0;

        for (StagedItem& item : items)
        {
            if (context.Wait(item.Future, k_UploadTimeoutNs) == rhi::FutureState::Complete)
            {
                item.Resource = item.Future.Get();
            }
            else
            {
                failures += 1;
            }

            item.Future = rhi::ResourceFuture<GraphicsResource>{};
        }

        return failures;
    }

    bool FormatCanBeUploadedAndReadBack(VkPhysicalDevice physical_device, VkFormat format)
    {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical_device, format, &properties);

        constexpr VkFormatFeatureFlags k_Needed = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        return (properties.optimalTilingFeatures & k_Needed) == k_Needed;
    }

    bool WaitUntilRingIsFree(const ResourceContext& context)
    {
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (context.Counters().StagingRingBytesInUse == 0)
            {
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return false;
    }
}
