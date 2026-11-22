#include "Device.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFuture.hpp"
#include "ResourcePlacement.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include "Semaphore.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;
    // long enough that a set that should not be held would have finished, short enough to keep the suite fast
    constexpr uint64_t k_HoldWindowNs = 100'000'000ull;
    constexpr uint32_t k_SentinelWord = 0xA5C3E1F7u;
    constexpr uint32_t k_ImageEdge = 16u;
    constexpr size_t k_ImageBytes = size_t{ k_ImageEdge } * k_ImageEdge * sizeof(uint32_t);

    constexpr rhi::ImageUsageTransition k_Resting{ ImageUsageBits::SampledImage, ImageUsageBits::SampledImage };
    constexpr rhi::ImageUsageTransition k_DiscardToResting{ ImageUsageBits::Discard, ImageUsageBits::SampledImage };

    [[nodiscard]] BufferCreateInfo DeviceLocalBuffer(uint64_t size_bytes);
    [[nodiscard]] BufferCreateInfo HostVisibleBuffer(uint64_t size_bytes);
    [[nodiscard]] ImageCreateInfo SquareImageInfo(uint32_t edge, uint32_t mip_levels);
    [[nodiscard]] std::vector<uint32_t> MakeSentinelWords(size_t count);
    [[nodiscard]] ImageRegion WholeMip(int32_t mip_level, int32_t edge);
    [[nodiscard]] BufferImageRegion MakeBufferImageRegion(uint64_t buffer_offset, const ImageRegion& image);
    [[nodiscard]] TransferSetOp MakeUploadBufferOp(const GraphicsResource& destination, uint64_t data_offset, uint64_t destination_offset, uint64_t size);
    [[nodiscard]] TransferSetOp MakeUploadImageOp(const GraphicsResource& destination, rhi::ImageUsageTransition usage, const BufferImageRegion& region);
    [[nodiscard]] TransferSetOp MakeFillOp(const GraphicsResource& destination, uint32_t value, uint64_t offset, uint64_t size);
    [[nodiscard]] TransferSetOp MakeCopyBufferOp(const GraphicsResource& source, const GraphicsResource& destination, const BufferCopyRegion& region);
    [[nodiscard]] TransferSetOp MakeCopyBufferToImageOp(const GraphicsResource& source, const GraphicsResource& destination, rhi::ImageUsageTransition usage, const BufferImageRegion& region);
    [[nodiscard]] TransferSetOp MakeCopyImageToBufferOp(const GraphicsResource& source, rhi::ImageUsageTransition usage, const GraphicsResource& destination, const BufferImageRegion& region);
    [[nodiscard]] TransferSetOp MakeCopyImageOp(const GraphicsResource& source, rhi::ImageUsageTransition source_usage, const GraphicsResource& destination, rhi::ImageUsageTransition destination_usage, const ImageCopyRegion& region);
    [[nodiscard]] TransferSetDesc MakeSet(std::span<const TransferSetOp> ops, std::span<const int32_t> range_ends, std::span<const std::byte> data);

    /** @brief Signals the timeline when the test ends, and then waits for the future. A failed ASSERT returns from the
     *  test early, and a set that still waits on the timeline would hang the shutdown of the context. Declare the guard
     *  after the timeline and the future, so that it is destroyed before both. */
    class TimelineReleaseGuard
    {
    public:
        TimelineReleaseGuard(ResourceContext& _context, rhi::TimelineSemaphore& _timeline, const rhi::OperationFuture& _future);
        ~TimelineReleaseGuard();
        TimelineReleaseGuard(const TimelineReleaseGuard&) = delete;
        TimelineReleaseGuard& operator=(const TimelineReleaseGuard&) = delete;

    private:
        ResourceContext& context;
        rhi::TimelineSemaphore& timeline;
        const rhi::OperationFuture& future;
    };
}

/** @brief The fixture of the transfer set tests: small helpers that make the buffers and images that the sets touch. */
class TransferSetTest : public ResourceContextTest
{
protected:
    [[nodiscard]] GraphicsResource CreateBufferWithWords(uint64_t size_bytes, const std::vector<uint32_t>& words);
    [[nodiscard]] GraphicsResource CreateImageWithWords(const ImageCreateInfo& info, const std::vector<uint32_t>& words);
    [[nodiscard]] GraphicsResource CreateEmptyImage(const ImageCreateInfo& info);
    /** @brief Submits the set with no waits, and waits for it. Returns the future, for its state and error. */
    [[nodiscard]] rhi::OperationFuture SubmitAndWait(const TransferSetDesc& set);
};

TEST_F(TransferSetTest, AMixedSetOfOneRangeLandsEveryOp)
{
    constexpr uint64_t k_BufferBytes = 1024u;
    const std::vector<uint32_t> sentinel = MakeSentinelWords(k_BufferBytes / sizeof(uint32_t));
    const std::vector<uint32_t> sourceWords = MakeWordPattern(64u, 301u);
    const GraphicsResource uploadedA = CreateBufferWithWords(k_BufferBytes, sentinel);
    const GraphicsResource uploadedB = CreateBufferWithWords(k_BufferBytes, sentinel);
    const GraphicsResource filled = CreateBufferWithWords(k_BufferBytes, sentinel);
    const GraphicsResource copySource = CreateBufferWithWords(256u, sourceWords);
    const GraphicsResource copyDestination = CreateBufferWithWords(256u, std::vector<uint32_t>(64u, k_SentinelWord));

    const std::vector<uint32_t> imageWords = MakeWordPattern(k_ImageEdge * k_ImageEdge, 302u);
    const GraphicsResource image = CreateImageWithWords(SquareImageInfo(k_ImageEdge, 1u), imageWords);

    // the data of the set: 64 words for A, 128 words for B, and an 8 by 4 rectangle of texels
    const std::vector<uint32_t> wordsForA = MakeWordPattern(64u, 303u);
    const std::vector<uint32_t> wordsForB = MakeWordPattern(128u, 304u);
    const std::vector<uint32_t> rectangle = MakeWordPattern(32u, 305u);
    std::vector<uint32_t> data(224u, 0u);
    std::copy(wordsForA.begin(), wordsForA.end(), data.begin());
    std::copy(wordsForB.begin(), wordsForB.end(), data.begin() + 64);
    std::copy(rectangle.begin(), rectangle.end(), data.begin() + 192);

    const ImageRegion rectangleBox{ 0, 0, 1, 4, 6, 0, 8, 4, 1 };
    const std::array<TransferSetOp, 5> ops
    {
        MakeUploadBufferOp(uploadedA, 0u, 128u, 256u),
        MakeUploadBufferOp(uploadedB, 256u, 0u, 512u),
        MakeFillOp(filled, 0xDEADBEEFu, 64u, 128u),
        MakeCopyBufferOp(copySource, copyDestination, BufferCopyRegion{ 32u, 16u, 128u }),
        MakeUploadImageOp(image, k_Resting, MakeBufferImageRegion(768u, rectangleBox)),
    };
    const std::array<int32_t, 1> rangeEnds{ 5 };

    rhi::OperationFuture reply = SubmitAndWait(MakeSet(ops, rangeEnds, std::as_bytes(std::span{ data })));
    ASSERT_EQ(reply.State(), rhi::FutureState::Complete);

    std::vector<uint32_t> expectedA = sentinel;
    std::copy(wordsForA.begin(), wordsForA.end(), expectedA.begin() + 32);
    EXPECT_EQ(ReadBackWords(uploadedA, k_BufferBytes), expectedA);

    std::vector<uint32_t> expectedB = sentinel;
    std::copy(wordsForB.begin(), wordsForB.end(), expectedB.begin());
    EXPECT_EQ(ReadBackWords(uploadedB, k_BufferBytes), expectedB);

    std::vector<uint32_t> expectedFilled = sentinel;
    std::fill(expectedFilled.begin() + 16, expectedFilled.begin() + 48, 0xDEADBEEFu);
    EXPECT_EQ(ReadBackWords(filled, k_BufferBytes), expectedFilled);

    std::vector<uint32_t> expectedCopy(64u, k_SentinelWord);
    std::copy(sourceWords.begin() + 8, sourceWords.begin() + 40, expectedCopy.begin() + 4);
    EXPECT_EQ(ReadBackWords(copyDestination, 256u), expectedCopy);

    std::vector<uint32_t> expectedImage = imageWords;
    for (int32_t row = 0; row < 4; ++row)
    {
        for (int32_t column = 0; column < 8; ++column)
        {
            expectedImage[static_cast<size_t>((6 + row) * 16 + 4 + column)] = rectangle[static_cast<size_t>(row * 8 + column)];
        }
    }

    EXPECT_EQ(ReadBackImageWords(image, k_Resting, k_ImageBytes), expectedImage);
    EXPECT_EQ(ReadBackWords(copySource, 256u), sourceWords);
}

TEST_F(TransferSetTest, ASecondRangeCopiesWhatTheFirstRangeUploaded)
{
    constexpr uint64_t k_BufferBytes = k_ImageBytes;
    const std::vector<uint32_t> words = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 311u);
    const GraphicsResource staged = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource copied = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource image = CreateEmptyImage(SquareImageInfo(k_ImageEdge, 1u));

    // range 1 uploads. range 2 reads the buffer that range 1 wrote, into a buffer and into an image
    const std::array<TransferSetOp, 3> ops
    {
        MakeUploadBufferOp(staged, 0u, 0u, k_BufferBytes),
        MakeCopyBufferOp(staged, copied, BufferCopyRegion{ 0u, 0u, k_BufferBytes }),
        MakeCopyBufferToImageOp(staged, image, k_DiscardToResting, MakeBufferImageRegion(0u, WholeMip(0, static_cast<int32_t>(k_ImageEdge)))),
    };
    const std::array<int32_t, 2> rangeEnds{ 1, 3 };

    rhi::OperationFuture reply = SubmitAndWait(MakeSet(ops, rangeEnds, std::as_bytes(std::span{ words })));
    ASSERT_EQ(reply.State(), rhi::FutureState::Complete);

    EXPECT_EQ(ReadBackWords(copied, k_BufferBytes), words);
    EXPECT_EQ(ReadBackImageWords(image, k_Resting, k_ImageBytes), words);
    EXPECT_EQ(ReadBackWords(staged, k_BufferBytes), words);
}

TEST_F(TransferSetTest, ADiscardedPlacedImageIsUploadedAndReadInOneSet)
{
    const ImageCreateInfo info = SquareImageInfo(k_ImageEdge, 1u);
    const rhi::placement::MemoryRequirements requirements = rhi::placement::GetImageMemoryRequirements(*context, info);
    ASSERT_GT(requirements.Size, 0u);

    const rhi::placement::MemoryHeapCreateInfo heapInfo{ requirements.Size, requirements.MemoryTypeBits, "TransferSetHeap" };
    rhi::ResourceFuture<GraphicsResource> heapReply = rhi::placement::CreateMemoryHeap(*context, heapInfo);
    ASSERT_EQ(context->Wait(heapReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource heap = heapReply.Get();

    rhi::ResourceFuture<GraphicsResource> imageReply = rhi::placement::CreatePlacedImage(*context, info, rhi::placement::Placement{ heap, 0u });
    ASSERT_EQ(context->Wait(imageReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = imageReply.Get();

    const std::vector<uint32_t> words = MakeWordPattern(k_ImageEdge * k_ImageEdge, 321u);
    const GraphicsResource result = CreateBufferWithWords(k_ImageBytes, MakeSentinelWords(k_ImageEdge * k_ImageEdge));

    // range 1 writes the whole mip from UNDEFINED, and range 2 reads it back into a buffer
    const ImageRegion wholeMip = WholeMip(0, static_cast<int32_t>(k_ImageEdge));
    const std::array<TransferSetOp, 2> ops
    {
        MakeUploadImageOp(image, k_DiscardToResting, MakeBufferImageRegion(0u, wholeMip)),
        MakeCopyImageToBufferOp(image, k_Resting, result, MakeBufferImageRegion(0u, wholeMip)),
    };
    const std::array<int32_t, 2> rangeEnds{ 1, 2 };

    rhi::OperationFuture reply = SubmitAndWait(MakeSet(ops, rangeEnds, std::as_bytes(std::span{ words })));
    ASSERT_EQ(reply.State(), rhi::FutureState::Complete);

    EXPECT_EQ(ReadBackWords(result, k_ImageBytes), words);
    EXPECT_EQ(ReadBackImageWords(image, k_Resting, k_ImageBytes), words);

    DestroyAndWait(image);
    DestroyAndWait(heap);
}

TEST_F(TransferSetTest, AnImageCopyBetweenTwoImagesInASet)
{
    const std::vector<uint32_t> sourceWords = MakeWordPattern(k_ImageEdge * k_ImageEdge, 331u);
    const std::vector<uint32_t> destinationWords = MakeWordPattern(k_ImageEdge * k_ImageEdge, 332u);
    const GraphicsResource source = CreateImageWithWords(SquareImageInfo(k_ImageEdge, 1u), sourceWords);
    const GraphicsResource destination = CreateImageWithWords(SquareImageInfo(k_ImageEdge, 1u), destinationWords);

    // a 6 by 5 box from (2, 3) of the source to (7, 8) of the destination
    const ImageCopyRegion region{ ImageRegion{ 0, 0, 1, 2, 3, 0, 6, 5, 1 }, ImageRegion{ 0, 0, 1, 7, 8, 0, 0, 0, 0 } };
    const std::array<TransferSetOp, 1> ops{ MakeCopyImageOp(source, k_Resting, destination, k_Resting, region) };
    const std::array<int32_t, 1> rangeEnds{ 1 };

    rhi::OperationFuture reply = SubmitAndWait(MakeSet(ops, rangeEnds, {}));
    ASSERT_EQ(reply.State(), rhi::FutureState::Complete);

    std::vector<uint32_t> expected = destinationWords;
    for (int32_t row = 0; row < 5; ++row)
    {
        for (int32_t column = 0; column < 6; ++column)
        {
            expected[static_cast<size_t>((8 + row) * 16 + 7 + column)] = sourceWords[static_cast<size_t>((3 + row) * 16 + 2 + column)];
        }
    }

    EXPECT_EQ(ReadBackImageWords(destination, k_Resting, k_ImageBytes), expected);
    EXPECT_EQ(ReadBackImageWords(source, k_Resting, k_ImageBytes), sourceWords);
}

TEST_F(TransferSetTest, AWaitHoldsTheWholeSetUntilTheTimelineIsSignaled)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    rhi::OperationFuture reply;
    TimelineReleaseGuard guard(*context, timeline, reply);

    constexpr uint64_t k_BufferBytes = 256u;
    const std::vector<uint32_t> words = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 341u);
    const GraphicsResource destination = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource filled = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));

    const std::array<TransferSetOp, 2> ops
    {
        MakeUploadBufferOp(destination, 0u, 0u, k_BufferBytes),
        MakeFillOp(filled, 7u, 0u, k_BufferBytes),
    };
    const std::array<int32_t, 1> rangeEnds{ 2 };
    const rhi::TransferWait wait{ timeline.Handle(), 1u };

    reply = context->SubmitTransferSet(MakeSet(ops, rangeEnds, std::as_bytes(std::span{ words })), std::span{ &wait, 1u });
    EXPECT_EQ(context->Wait(reply, k_HoldWindowNs), rhi::FutureState::Created);

    ASSERT_TRUE(timeline.SetValue(1u).IsSuccess());
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackWords(destination, k_BufferBytes), words);
    EXPECT_EQ(ReadBackWords(filled, k_BufferBytes), std::vector<uint32_t>(k_BufferBytes / sizeof(uint32_t), 7u));
}

TEST_F(TransferSetTest, EveryTouchedResourceWaitsForTheTicketOfTheSet)
{
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    rhi::OperationFuture reply;
    TimelineReleaseGuard guard(*context, timeline, reply);

    constexpr uint64_t k_BufferBytes = 256u;
    const GraphicsResource source = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource destination = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));

    const std::array<TransferSetOp, 1> ops{ MakeCopyBufferOp(source, destination, BufferCopyRegion{ 0u, 0u, k_BufferBytes }) };
    const std::array<int32_t, 1> rangeEnds{ 1 };
    const rhi::TransferWait wait{ timeline.Handle(), 1u };
    reply = context->SubmitTransferSet(MakeSet(ops, rangeEnds, {}), std::span{ &wait, 1u });
    ASSERT_EQ(context->Wait(reply, k_HoldWindowNs), rhi::FutureState::Created);

    // the set only reads the source, and writes the destination. both must stay alive while the set waits
    rhi::OperationFuture destroySource = context->DestroyResource(source);
    rhi::OperationFuture destroyDestination = context->DestroyResource(destination);
    EXPECT_NE(context->Wait(destroySource, k_HoldWindowNs), rhi::FutureState::Complete);
    EXPECT_NE(context->Wait(destroyDestination, k_HoldWindowNs), rhi::FutureState::Complete);

    ASSERT_TRUE(timeline.SetValue(1u).IsSuccess());
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(context->Wait(destroySource, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(context->Wait(destroyDestination, k_TimeoutNs), rhi::FutureState::Complete);
}

TEST_F(TransferSetTest, AWholeSetHasOneTicket)
{
    constexpr uint64_t k_BufferBytes = 256u;
    const GraphicsResource first = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource second = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource third = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource fourth = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));

    rhi::OperationFuture probeBefore = context->FillBuffer(first, 1u, 0u, k_BufferBytes, {});
    ASSERT_EQ(context->Wait(probeBefore, k_TimeoutNs), rhi::FutureState::Complete);
    const uint64_t probeTicket = probeBefore.Completion().Value;
    const int64_t recordedBefore = context->Counters().TransfersRecorded;

    // four ops over two ranges: two fills, and two copies that read what the fills wrote
    const std::array<TransferSetOp, 4> ops
    {
        MakeFillOp(first, 2u, 0u, k_BufferBytes),
        MakeFillOp(second, 3u, 0u, k_BufferBytes),
        MakeCopyBufferOp(first, third, BufferCopyRegion{ 0u, 0u, k_BufferBytes }),
        MakeCopyBufferOp(second, fourth, BufferCopyRegion{ 0u, 0u, 64u }),
    };
    const std::array<int32_t, 2> rangeEnds{ 2, 4 };
    rhi::OperationFuture setReply = SubmitAndWait(MakeSet(ops, rangeEnds, {}));
    ASSERT_EQ(setReply.State(), rhi::FutureState::Complete);
    EXPECT_EQ(setReply.Completion().Value, probeTicket + 1u);
    EXPECT_EQ(context->Counters().TransfersRecorded - recordedBefore, 1);

    // no ticket was used up by the ops of the set
    rhi::OperationFuture probeAfter = context->FillBuffer(first, 4u, 0u, k_BufferBytes, {});
    ASSERT_EQ(context->Wait(probeAfter, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(probeAfter.Completion().Value, probeTicket + 2u);
}

TEST_F(TransferSetTest, EveryRefusalFailsTheWholeSetAndChangesNothing)
{
    constexpr uint64_t k_BufferBytes = 256u;
    constexpr uint64_t k_FarOffset = std::numeric_limits<uint64_t>::max() - 3u;
    const std::vector<uint32_t> destinationWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 351u);
    const std::vector<uint32_t> canaryWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 352u);
    const GraphicsResource destination = CreateBufferWithWords(k_BufferBytes, destinationWords);
    const GraphicsResource source = CreateBufferWithWords(k_BufferBytes, MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 353u));
    const GraphicsResource canary = CreateBufferWithWords(k_BufferBytes, canaryWords);
    const GraphicsResource hostVisible = CreateBufferAndWait(HostVisibleBuffer(k_BufferBytes), {});
    const GraphicsResource noTransferUsage = CreateBufferAndWait(BufferCreateInfo{ ResourceDomain::GPUOnly, ResourceCreationFlags::None, BufferUsageBits::ComputeShaderUAV, k_BufferBytes }, {});
    const GraphicsResource image = CreateImageWithWords(SquareImageInfo(k_ImageEdge, 1u), MakeWordPattern(k_ImageEdge * k_ImageEdge, 354u));
    const GraphicsResource staleBuffer = CreateBufferWithWords(k_BufferBytes, canaryWords);
    DestroyAndWait(staleBuffer);

    const std::vector<uint32_t> goodWords = MakeWordPattern(64u, 355u);
    const std::span<const std::byte> goodData = std::as_bytes(std::span{ goodWords });
    // the first op of every bad set writes the canary, and must not happen
    const TransferSetOp canaryOp = MakeUploadBufferOp(canary, 0u, 0u, k_BufferBytes);

    struct Case
    {
        std::string_view Name;
        std::vector<TransferSetOp> Ops;
        std::vector<int32_t> RangeEnds;
        std::span<const std::byte> Data;
        rhi::ResourceError Expected;
    };

    std::vector<Case> cases;
    const auto addCase = [&](std::string_view name, TransferSetOp bad_op, rhi::ResourceError expected)
    {
        cases.push_back(Case{ name, { canaryOp, bad_op }, { 2 }, goodData, expected });
    };

    const ImageRegion wholeMip = WholeMip(0, static_cast<int32_t>(k_ImageEdge));
    const ImageRegion rectangle{ 0, 0, 1, 2, 2, 0, 4, 4, 1 };

    // shape errors, found on the calling thread
    cases.push_back(Case{ "no op", {}, { 0 }, goodData, rhi::ResourceError::InvalidTransferSet });
    cases.push_back(Case{ "257 ops", std::vector<TransferSetOp>(257u, canaryOp), { 257 }, goodData, rhi::ResourceError::InvalidTransferSet });
    cases.push_back(Case{ "no range end", { canaryOp, canaryOp }, {}, goodData, rhi::ResourceError::InvalidTransferSet });
    cases.push_back(Case{ "last range end below the op count", { canaryOp, canaryOp }, { 1 }, goodData, rhi::ResourceError::InvalidTransferSet });
    cases.push_back(Case{ "last range end above the op count", { canaryOp, canaryOp }, { 3 }, goodData, rhi::ResourceError::InvalidTransferSet });
    cases.push_back(Case{ "range ends that do not rise", { canaryOp, canaryOp }, { 2, 1 }, goodData, rhi::ResourceError::InvalidTransferSet });
    cases.push_back(Case{ "an empty range in the middle", { canaryOp, canaryOp }, { 1, 1, 2 }, goodData, rhi::ResourceError::InvalidTransferSet });
    cases.push_back(Case{ "an empty first range", { canaryOp, canaryOp }, { 0, 2 }, goodData, rhi::ResourceError::InvalidTransferSet });
    cases.push_back(Case{ "a negative range end", { canaryOp, canaryOp }, { -1, 2 }, goodData, rhi::ResourceError::InvalidTransferSet });

    // errors of the ops, found on the worker
    TransferSetOp invalidKind = canaryOp;
    invalidKind.Kind = TransferOpKind::Invalid;
    addCase("an op of the invalid kind", invalidKind, rhi::ResourceError::InvalidTransferSet);
    addCase("upload bytes that pass the end of the data", MakeUploadBufferOp(destination, 224u, 0u, 64u), rhi::ResourceError::InvalidTransferSet);
    addCase("upload bytes that start after the data", MakeUploadBufferOp(destination, 512u, 0u, 4u), rhi::ResourceError::InvalidTransferSet);
    addCase("upload bytes with an offset that overflows", MakeUploadBufferOp(destination, k_FarOffset, 0u, 8u), rhi::ResourceError::InvalidTransferSet);
    addCase("image texels that pass the end of the data", MakeUploadImageOp(image, k_Resting, MakeBufferImageRegion(0u, wholeMip)), rhi::ResourceError::InvalidTransferSet);
    addCase("upload into host-visible memory", MakeUploadBufferOp(hostVisible, 0u, 0u, 64u), rhi::ResourceError::InvalidTransferSet);
    addCase("copy into host-visible memory", MakeCopyBufferOp(source, hostVisible, BufferCopyRegion{ 0u, 0u, 64u }), rhi::ResourceError::InvalidTransferSet);
    addCase("fill of host-visible memory", MakeFillOp(hostVisible, 1u, 0u, 64u), rhi::ResourceError::InvalidTransferSet);
    addCase("a buffer copy onto itself", MakeCopyBufferOp(destination, destination, BufferCopyRegion{ 0u, 128u, 64u }), rhi::ResourceError::InvalidTransferSet);
    addCase("an image copy onto itself", MakeCopyImageOp(image, k_Resting, image, k_Resting, ImageCopyRegion{ ImageRegion{ 0, 0, 1, 0, 0, 0, 2, 2, 1 }, ImageRegion{ 0, 0, 1, 8, 8, 0, 0, 0, 0 } }), rhi::ResourceError::InvalidTransferSet);

    addCase("upload past the end of the destination", MakeUploadBufferOp(destination, 0u, 224u, 64u), rhi::ResourceError::InvalidRegion);
    addCase("upload with an offset that is not a multiple of 4", MakeUploadBufferOp(destination, 0u, 2u, 8u), rhi::ResourceError::InvalidRegion);
    addCase("upload of size 0", MakeUploadBufferOp(destination, 0u, 0u, 0u), rhi::ResourceError::InvalidRegion);
    addCase("copy of size 0", MakeCopyBufferOp(source, destination, BufferCopyRegion{ 0u, 0u, 0u }), rhi::ResourceError::InvalidRegion);
    addCase("copy past the end of the source", MakeCopyBufferOp(source, destination, BufferCopyRegion{ 224u, 0u, 64u }), rhi::ResourceError::InvalidRegion);
    addCase("copy with a size that overflows", MakeCopyBufferOp(source, destination, BufferCopyRegion{ 4u, 4u, k_FarOffset }), rhi::ResourceError::InvalidRegion);
    addCase("fill past the end", MakeFillOp(destination, 1u, 224u, 64u), rhi::ResourceError::InvalidRegion);
    addCase("fill of size 0", MakeFillOp(destination, 1u, 0u, 0u), rhi::ResourceError::InvalidRegion);
    addCase("fill with an offset that is not a multiple of 4", MakeFillOp(destination, 1u, 2u, 8u), rhi::ResourceError::InvalidFillRange);
    addCase("fill with a size that is not a multiple of 4", MakeFillOp(destination, 1u, 0u, 6u), rhi::ResourceError::InvalidFillRange);

    const ImageRegion outsideImage{ 0, 0, 1, 14, 14, 0, 4, 4, 1 };
    addCase("an image box outside the image", MakeCopyBufferToImageOp(source, image, k_Resting, MakeBufferImageRegion(0u, outsideImage)), rhi::ResourceError::InvalidRegion);
    addCase("an image box that reads past the buffer", MakeCopyBufferToImageOp(source, image, k_Resting, MakeBufferImageRegion(224u, rectangle)), rhi::ResourceError::InvalidRegion);
    addCase("a discard that does not cover the whole mip level", MakeCopyBufferToImageOp(source, image, k_DiscardToResting, MakeBufferImageRegion(0u, rectangle)), rhi::ResourceError::InvalidRegion);
    addCase("an image to buffer box that does not fit the buffer", MakeCopyImageToBufferOp(image, k_Resting, destination, MakeBufferImageRegion(0u, wholeMip)), rhi::ResourceError::InvalidRegion);
    addCase("an image box with no extent", MakeCopyBufferToImageOp(source, image, k_Resting, MakeBufferImageRegion(0u, ImageRegion{ 0, 0, 1, 0, 0, 0, 0, 4, 1 })), rhi::ResourceError::InvalidRegion);

    addCase("a destination with no transfer usage", MakeUploadBufferOp(noTransferUsage, 0u, 0u, 64u), rhi::ResourceError::MissingTransferUsage);
    addCase("a source with no transfer usage", MakeCopyBufferOp(noTransferUsage, destination, BufferCopyRegion{ 0u, 0u, 64u }), rhi::ResourceError::MissingTransferUsage);
    addCase("an image where a buffer is needed", MakeUploadBufferOp(image, 0u, 0u, 64u), rhi::ResourceError::WrongResourceKind);
    addCase("a buffer where an image is needed", MakeUploadImageOp(destination, k_Resting, MakeBufferImageRegion(0u, rectangle)), rhi::ResourceError::WrongResourceKind);
    addCase("a destroyed buffer", MakeUploadBufferOp(staleBuffer, 0u, 0u, 64u), rhi::ResourceError::StaleResource);
    addCase("an image usage that the image does not have", MakeCopyBufferToImageOp(source, image, rhi::ImageUsageTransition{ ImageUsageBits::RenderTargetWrite, ImageUsageBits::SampledImage }, MakeBufferImageRegion(0u, rectangle)), rhi::ResourceError::InvalidUsageTransition);
    addCase("a discard after the transfer", MakeCopyBufferToImageOp(source, image, rhi::ImageUsageTransition{ ImageUsageBits::SampledImage, ImageUsageBits::Discard }, MakeBufferImageRegion(0u, rectangle)), rhi::ResourceError::InvalidUsageTransition);
    addCase("a discard before an image that is read", MakeCopyImageToBufferOp(image, k_DiscardToResting, destination, MakeBufferImageRegion(0u, rectangle)), rhi::ResourceError::InvalidUsageTransition);

    std::vector<rhi::OperationFuture> futures;
    for (const Case& entry : cases)
    {
        futures.push_back(context->SubmitTransferSet(MakeSet(entry.Ops, entry.RangeEnds, entry.Data), {}));
    }

    for (size_t index = 0u; index < cases.size(); ++index)
    {
        const Case& entry = cases[index];
        EXPECT_EQ(context->Wait(futures[index], k_TimeoutNs), rhi::FutureState::Failed) << entry.Name;
        EXPECT_EQ(futures[index].Error(), entry.Expected)
            << entry.Name << ": got " << rhi::ToString(futures[index].Error()) << ", expected " << rhi::ToString(entry.Expected);
    }

    // too many waits, and a wait that cannot be waited for, as in every other call
    rhi::TimelineSemaphore timeline(rhiSystem->GetDevice()->Handle());
    const std::array<TransferSetOp, 1> oneOp{ canaryOp };
    const std::array<int32_t, 1> oneRange{ 1 };
    const std::array<rhi::TransferWait, 5> tooManyWaits
    {
        rhi::TransferWait{ timeline.Handle(), 1u }, rhi::TransferWait{ timeline.Handle(), 1u }, rhi::TransferWait{ timeline.Handle(), 1u },
        rhi::TransferWait{ timeline.Handle(), 1u }, rhi::TransferWait{ timeline.Handle(), 1u },
    };
    rhi::OperationFuture tooManyReply = context->SubmitTransferSet(MakeSet(oneOp, oneRange, goodData), tooManyWaits);
    EXPECT_EQ(context->Wait(tooManyReply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(tooManyReply.Error(), rhi::ResourceError::TooManyWaits);

    const rhi::TransferWait nullSemaphore{ rhi::TimelineSemaphoreHandle{}, 1u };
    rhi::OperationFuture nullReply = context->SubmitTransferSet(MakeSet(oneOp, oneRange, goodData), std::span{ &nullSemaphore, 1u });
    EXPECT_EQ(context->Wait(nullReply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(nullReply.Error(), rhi::ResourceError::InvalidWait);

    // not one of the bad sets wrote anything, and not one of them wrote its good first op
    EXPECT_EQ(ReadBackWords(destination, k_BufferBytes), destinationWords);
    EXPECT_EQ(ReadBackWords(canary, k_BufferBytes), canaryWords);
    EXPECT_EQ(ReadBackImageWords(image, k_Resting, k_ImageBytes), MakeWordPattern(k_ImageEdge * k_ImageEdge, 354u));
    EXPECT_EQ(context->Counters().TransferSetsRecorded, 0);
    EXPECT_EQ(context->Counters().TransfersFailed, 0);
}

TEST_F(TransferSetTest, AnOpWhoseRegionDoesNotMatchItsKindFailsTheWholeSet)
{
    constexpr uint64_t k_BufferBytes = 256u;
    const std::vector<uint32_t> canaryWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 361u);
    const GraphicsResource canary = CreateBufferWithWords(k_BufferBytes, canaryWords);
    const GraphicsResource source = CreateBufferWithWords(k_BufferBytes, MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 362u));
    const std::vector<uint32_t> destinationWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 363u);
    const GraphicsResource destination = CreateBufferWithWords(k_BufferBytes, destinationWords);
    const std::vector<uint32_t> imageWords = MakeWordPattern(k_ImageEdge * k_ImageEdge, 364u);
    const GraphicsResource image = CreateImageWithWords(SquareImageInfo(k_ImageEdge, 1u), imageWords);
    const GraphicsResource otherImage = CreateImageWithWords(SquareImageInfo(k_ImageEdge, 1u), imageWords);

    const std::vector<uint32_t> goodWords = MakeWordPattern(64u, 365u);
    const std::span<const std::byte> goodData = std::as_bytes(std::span{ goodWords });
    const TransferSetOp canaryOp = MakeUploadBufferOp(canary, 0u, 0u, k_BufferBytes);

    const BufferCopyRegion bufferRegion{ 0u, 0u, 64u };
    const BufferImageRegion boxRegion = MakeBufferImageRegion(0u, WholeMip(0, static_cast<int32_t>(k_ImageEdge)));
    const ImageCopyRegion imageRegion{ WholeMip(0, static_cast<int32_t>(k_ImageEdge)), WholeMip(0, static_cast<int32_t>(k_ImageEdge)) };

    // each op is valid in every way except that its Region holds the alternative of another kind
    TransferSetOp uploadBuffer = MakeUploadBufferOp(destination, 0u, 0u, 64u);
    uploadBuffer.Region = boxRegion;
    TransferSetOp uploadImage = MakeUploadImageOp(image, k_Resting, boxRegion);
    uploadImage.Region = bufferRegion;
    TransferSetOp fillBuffer = MakeFillOp(destination, 1u, 0u, 64u);
    fillBuffer.Region = imageRegion;
    TransferSetOp copyBuffer = MakeCopyBufferOp(source, destination, bufferRegion);
    copyBuffer.Region = boxRegion;
    TransferSetOp copyBufferToImage = MakeCopyBufferToImageOp(source, image, k_Resting, boxRegion);
    copyBufferToImage.Region = imageRegion;
    TransferSetOp copyImageToBuffer = MakeCopyImageToBufferOp(image, k_Resting, destination, boxRegion);
    copyImageToBuffer.Region = bufferRegion;
    TransferSetOp copyImage = MakeCopyImageOp(image, k_Resting, otherImage, k_Resting, imageRegion);
    copyImage.Region = boxRegion;

    const std::array<TransferSetOp, 7> badOps{ uploadBuffer, uploadImage, fillBuffer, copyBuffer, copyBufferToImage, copyImageToBuffer, copyImage };
    const std::array<std::string_view, 7> names
    {
        "UploadBuffer with a box", "UploadImage with a range", "FillBuffer with an image copy", "CopyBuffer with a box",
        "CopyBufferToImage with an image copy", "CopyImageToBuffer with a range", "CopyImage with a box",
    };

    const std::array<int32_t, 1> oneRange{ 2 };
    for (size_t index = 0u; index < badOps.size(); ++index)
    {
        const std::array<TransferSetOp, 2> ops{ canaryOp, badOps[index] };
        rhi::OperationFuture future = context->SubmitTransferSet(MakeSet(ops, oneRange, goodData), {});
        EXPECT_EQ(context->Wait(future, k_TimeoutNs), rhi::FutureState::Failed) << names[index];
        EXPECT_EQ(future.Error(), rhi::ResourceError::InvalidTransferSet) << names[index];
    }

    // not one refused set wrote anything, and not one of them wrote its good first op
    EXPECT_EQ(ReadBackWords(destination, k_BufferBytes), destinationWords);
    EXPECT_EQ(ReadBackWords(canary, k_BufferBytes), canaryWords);
    EXPECT_EQ(ReadBackImageWords(image, k_Resting, k_ImageBytes), imageWords);
    EXPECT_EQ(context->Counters().TransferSetsRecorded, 0);

    // the same op with a matching Region is accepted, so the refusals above come from the mismatch alone
    const std::array<TransferSetOp, 1> goodOps{ MakeUploadBufferOp(destination, 0u, 0u, 64u) };
    const std::array<int32_t, 1> goodRange{ 1 };
    rhi::OperationFuture goodFuture = context->SubmitTransferSet(MakeSet(goodOps, goodRange, goodData), {});
    EXPECT_EQ(context->Wait(goodFuture, k_TimeoutNs), rhi::FutureState::Complete);

    EXPECT_EQ(context->Counters().TransferSetsRecorded, 1);
    EXPECT_EQ(context->Counters().TransfersFailed, 0);
}

TEST_F(TransferSetTest, TwoOpsOfOneRangeThatTouchOneResourceAreAHazard)
{
#ifdef NDEBUG
    GTEST_SKIP() << "the range hazard validator exists in debug builds only";
#else
    constexpr uint64_t k_BufferBytes = 256u;
    const GraphicsResource first = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource second = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource third = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const std::vector<uint32_t> words = MakeWordPattern(32u, 361u);
    const std::span<const std::byte> data = std::as_bytes(std::span{ words });

    // two writes of one buffer, to regions that do not overlap: still a hazard, because the rule is for each resource
    const std::array<TransferSetOp, 2> twoWrites
    {
        MakeUploadBufferOp(first, 0u, 0u, 64u),
        MakeUploadBufferOp(first, 64u, 128u, 64u),
    };
    // a read and a write of one buffer
    const std::array<TransferSetOp, 2> readAndWrite
    {
        MakeCopyBufferOp(first, second, BufferCopyRegion{ 0u, 0u, 64u }),
        MakeFillOp(first, 9u, 128u, 64u),
    };
    // a write and a read of one buffer, in the other order
    const std::array<TransferSetOp, 2> writeAndRead
    {
        MakeFillOp(third, 9u, 128u, 64u),
        MakeCopyBufferOp(third, second, BufferCopyRegion{ 0u, 64u, 64u }),
    };
    const std::array<int32_t, 1> oneRange{ 2 };

    rhi::OperationFuture twoWritesReply = SubmitAndWait(MakeSet(twoWrites, oneRange, data));
    EXPECT_EQ(twoWritesReply.State(), rhi::FutureState::Failed);
    EXPECT_EQ(twoWritesReply.Error(), rhi::ResourceError::RangeHazard);

    rhi::OperationFuture readAndWriteReply = SubmitAndWait(MakeSet(readAndWrite, oneRange, {}));
    EXPECT_EQ(readAndWriteReply.State(), rhi::FutureState::Failed);
    EXPECT_EQ(readAndWriteReply.Error(), rhi::ResourceError::RangeHazard);

    rhi::OperationFuture writeAndReadReply = SubmitAndWait(MakeSet(writeAndRead, oneRange, {}));
    EXPECT_EQ(writeAndReadReply.State(), rhi::FutureState::Failed);
    EXPECT_EQ(writeAndReadReply.Error(), rhi::ResourceError::RangeHazard);

    EXPECT_EQ(context->Counters().TransferSetsRecorded, 0);

    // the same ops in two ranges are ordered, so they are accepted
    const std::array<int32_t, 2> twoRanges{ 1, 2 };
    rhi::OperationFuture twoWritesInTwoRanges = SubmitAndWait(MakeSet(twoWrites, twoRanges, data));
    EXPECT_EQ(twoWritesInTwoRanges.State(), rhi::FutureState::Complete);
    rhi::OperationFuture readAndWriteInTwoRanges = SubmitAndWait(MakeSet(readAndWrite, twoRanges, {}));
    EXPECT_EQ(readAndWriteInTwoRanges.State(), rhi::FutureState::Complete);

    // two reads of one buffer in one range are no hazard
    const std::array<TransferSetOp, 2> twoReads
    {
        MakeCopyBufferOp(first, second, BufferCopyRegion{ 0u, 0u, 64u }),
        MakeCopyBufferOp(first, third, BufferCopyRegion{ 64u, 0u, 64u }),
    };
    rhi::OperationFuture twoReadsReply = SubmitAndWait(MakeSet(twoReads, oneRange, {}));
    EXPECT_EQ(twoReadsReply.State(), rhi::FutureState::Complete);
#endif
}

TEST_F(TransferSetTest, ARangeCostsTwoBarrierCalls)
{
    const std::vector<uint32_t> mipZero = MakeWordPattern(256u, 371u);
    const std::vector<uint32_t> mipOne = MakeWordPattern(64u, 372u);
    const std::vector<uint32_t> mipTwo = MakeWordPattern(16u, 373u);
    std::vector<uint32_t> data;
    data.insert(data.end(), mipZero.begin(), mipZero.end());
    data.insert(data.end(), mipOne.begin(), mipOne.end());
    data.insert(data.end(), mipTwo.begin(), mipTwo.end());

    const GraphicsResource image = CreateEmptyImage(SquareImageInfo(k_ImageEdge, 3u));

    // three ranges, each with an image that changes layout: a barrier before the range and one after it
    const std::array<TransferSetOp, 3> ops
    {
        MakeUploadImageOp(image, k_DiscardToResting, MakeBufferImageRegion(0u, WholeMip(0, 16))),
        MakeUploadImageOp(image, k_DiscardToResting, MakeBufferImageRegion(1024u, WholeMip(1, 8))),
        MakeUploadImageOp(image, k_DiscardToResting, MakeBufferImageRegion(1280u, WholeMip(2, 4))),
    };
    const std::array<int32_t, 3> rangeEnds{ 1, 2, 3 };

    const ResourceContextCounters before = context->Counters();
    rhi::OperationFuture reply = SubmitAndWait(MakeSet(ops, rangeEnds, std::as_bytes(std::span{ data })));
    ASSERT_EQ(reply.State(), rhi::FutureState::Complete);
    const ResourceContextCounters after = context->Counters();

    EXPECT_EQ(after.TransferBarriersRecorded - before.TransferBarriersRecorded, 6);
    EXPECT_EQ(after.TransferSetsRecorded - before.TransferSetsRecorded, 1);
    EXPECT_EQ(after.TransfersRecorded - before.TransfersRecorded, 1);

    EXPECT_EQ(ReadBackImageWords(image, k_Resting, k_ImageBytes), mipZero);
}

TEST_F(TransferSetTest, SetsWithoutWaitsShareTheOpenBatch)
{
    // a large upload keeps the transfer thread busy, so that both sets are queued before it looks for more work
    constexpr uint64_t k_LargeBytes = 32ull * 1024ull * 1024ull;
    constexpr uint64_t k_BufferBytes = 256u;
    const std::vector<uint32_t> largeWords(k_LargeBytes / sizeof(uint32_t), 5u);
    const GraphicsResource large = CreateBufferAndWait(DeviceLocalBuffer(k_LargeBytes), {});
    const GraphicsResource first = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));
    const GraphicsResource second = CreateBufferWithWords(k_BufferBytes, MakeSentinelWords(k_BufferBytes / sizeof(uint32_t)));

    const std::array<TransferSetOp, 1> firstOps{ MakeFillOp(first, 1u, 0u, k_BufferBytes) };
    const std::array<TransferSetOp, 1> secondOps{ MakeFillOp(second, 2u, 0u, k_BufferBytes) };
    const std::array<int32_t, 1> oneRange{ 1 };
    const RhiBufferResourceData largeData{ std::as_bytes(std::span{ largeWords }), 0u };

    const int64_t batchesBefore = context->Counters().TransferBatchesSubmitted;
    rhi::OperationFuture largeReply = context->SetBufferData(large, std::span{ &largeData, 1u }, {});
    rhi::OperationFuture firstReply = context->SubmitTransferSet(MakeSet(firstOps, oneRange, {}), {});
    rhi::OperationFuture secondReply = context->SubmitTransferSet(MakeSet(secondOps, oneRange, {}), {});

    EXPECT_EQ(context->Wait(largeReply, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(context->Wait(firstReply, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(context->Wait(secondReply, k_TimeoutNs), rhi::FutureState::Complete);

    // the large upload and the two sets went out in one submit
    EXPECT_EQ(context->Counters().TransferBatchesSubmitted - batchesBefore, 1);
    EXPECT_EQ(ReadBackWords(first, k_BufferBytes), std::vector<uint32_t>(k_BufferBytes / sizeof(uint32_t), 1u));
    EXPECT_EQ(ReadBackWords(second, k_BufferBytes), std::vector<uint32_t>(k_BufferBytes / sizeof(uint32_t), 2u));
}

GraphicsResource TransferSetTest::CreateBufferWithWords(uint64_t size_bytes, const std::vector<uint32_t>& words)
{
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    return CreateBufferAndWait(DeviceLocalBuffer(size_bytes), std::span{ &data, 1u });
}

GraphicsResource TransferSetTest::CreateImageWithWords(const ImageCreateInfo& info, const std::vector<uint32_t>& words)
{
    RhiImageResourceData piece{};
    piece.Data = std::as_bytes(std::span{ words });
    piece.Width = static_cast<int32_t>(info.Width);
    piece.Height = static_cast<int32_t>(info.Height);

    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(info, std::span{ &piece, 1u }, ImageUsageBits::SampledImage);
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    return reply.Get();
}

GraphicsResource TransferSetTest::CreateEmptyImage(const ImageCreateInfo& info)
{
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(info);
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    return reply.Get();
}

rhi::OperationFuture TransferSetTest::SubmitAndWait(const TransferSetDesc& set)
{
    rhi::OperationFuture reply = context->SubmitTransferSet(set, {});
    static_cast<void>(context->Wait(reply, k_TimeoutNs));
    return reply;
}

namespace
{
    TimelineReleaseGuard::TimelineReleaseGuard(ResourceContext& _context, rhi::TimelineSemaphore& _timeline, const rhi::OperationFuture& _future) :
        context{ _context },
        timeline{ _timeline },
        future{ _future }
    {}

    TimelineReleaseGuard::~TimelineReleaseGuard()
    {
        uint64_t current = 0u;
        if (timeline.GetValue(current).IsSuccess() && current < 1u)
        {
            static_cast<void>(timeline.SetValue(1u));
        }

        // the semaphore is destroyed after this guard, so no set may still wait on it
        static_cast<void>(context.Wait(future, k_TimeoutNs));
    }

    BufferCreateInfo DeviceLocalBuffer(uint64_t size_bytes)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::TransferSource | BufferUsageBits::TransferDestination;
        info.Size = size_bytes;
        return info;
    }

    BufferCreateInfo HostVisibleBuffer(uint64_t size_bytes)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::CPUToGPU;
        info.Usage = BufferUsageBits::TransferSource | BufferUsageBits::TransferDestination;
        info.Size = size_bytes;
        return info;
    }

    ImageCreateInfo SquareImageInfo(uint32_t edge, uint32_t mip_levels)
    {
        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::TransferSource | ImageUsageBits::TransferDestination;
        info.Type = ImageType::Type2D;
        info.Format = ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
        info.Width = edge;
        info.Height = edge;
        info.Depth = 1u;
        info.MipLevels = mip_levels;
        info.ArrayLayers = 1u;
        return info;
    }

    std::vector<uint32_t> MakeSentinelWords(size_t count)
    {
        return std::vector<uint32_t>(count, k_SentinelWord);
    }

    ImageRegion WholeMip(int32_t mip_level, int32_t edge)
    {
        return ImageRegion{ mip_level, 0, 1, 0, 0, 0, edge, edge, 1 };
    }

    BufferImageRegion MakeBufferImageRegion(uint64_t buffer_offset, const ImageRegion& image)
    {
        return BufferImageRegion{ buffer_offset, 0, 0, image };
    }

    TransferSetOp MakeUploadBufferOp(const GraphicsResource& destination, uint64_t data_offset, uint64_t destination_offset, uint64_t size)
    {
        TransferSetOp op;
        op.Kind = TransferOpKind::UploadBuffer;
        op.Destination = destination;
        op.Region = BufferCopyRegion{ data_offset, destination_offset, size };
        return op;
    }

    TransferSetOp MakeUploadImageOp(const GraphicsResource& destination, rhi::ImageUsageTransition usage, const BufferImageRegion& region)
    {
        TransferSetOp op;
        op.Kind = TransferOpKind::UploadImage;
        op.Destination = destination;
        op.DestinationUsage = usage;
        op.Region = region;
        return op;
    }

    TransferSetOp MakeFillOp(const GraphicsResource& destination, uint32_t value, uint64_t offset, uint64_t size)
    {
        TransferSetOp op;
        op.Kind = TransferOpKind::FillBuffer;
        op.Destination = destination;
        op.Region = BufferCopyRegion{ 0u, offset, size };
        op.FillValue = value;
        return op;
    }

    TransferSetOp MakeCopyBufferOp(const GraphicsResource& source, const GraphicsResource& destination, const BufferCopyRegion& region)
    {
        TransferSetOp op;
        op.Kind = TransferOpKind::CopyBuffer;
        op.Source = source;
        op.Destination = destination;
        op.Region = region;
        return op;
    }

    TransferSetOp MakeCopyBufferToImageOp(const GraphicsResource& source, const GraphicsResource& destination, rhi::ImageUsageTransition usage, const BufferImageRegion& region)
    {
        TransferSetOp op;
        op.Kind = TransferOpKind::CopyBufferToImage;
        op.Source = source;
        op.Destination = destination;
        op.DestinationUsage = usage;
        op.Region = region;
        return op;
    }

    TransferSetOp MakeCopyImageToBufferOp(const GraphicsResource& source, rhi::ImageUsageTransition usage, const GraphicsResource& destination, const BufferImageRegion& region)
    {
        TransferSetOp op;
        op.Kind = TransferOpKind::CopyImageToBuffer;
        op.Source = source;
        op.SourceUsage = usage;
        op.Destination = destination;
        op.Region = region;
        return op;
    }

    TransferSetOp MakeCopyImageOp(const GraphicsResource& source, rhi::ImageUsageTransition source_usage, const GraphicsResource& destination, rhi::ImageUsageTransition destination_usage, const ImageCopyRegion& region)
    {
        TransferSetOp op;
        op.Kind = TransferOpKind::CopyImage;
        op.Source = source;
        op.SourceUsage = source_usage;
        op.Destination = destination;
        op.DestinationUsage = destination_usage;
        op.Region = region;
        return op;
    }

    TransferSetDesc MakeSet(std::span<const TransferSetOp> ops, std::span<const int32_t> range_ends, std::span<const std::byte> data)
    {
        return TransferSetDesc{ ops, range_ends, data };
    }
}
