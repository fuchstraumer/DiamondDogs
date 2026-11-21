#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;
    constexpr uint32_t k_SentinelWord = 0xA5C3E1F7u;

    [[nodiscard]] BufferCreateInfo DeviceLocalBuffer(uint64_t size_bytes);
    [[nodiscard]] BufferCreateInfo HostVisibleBuffer(uint64_t size_bytes);
    /** @brief Returns `count` words that all have the sentinel value. */
    [[nodiscard]] std::vector<uint32_t> MakeSentinelWords(size_t count);
    /** @brief Copies `piece` into `words`, starting at the byte offset. The offset must be a multiple of 4. */
    void WritePiece(std::vector<uint32_t>& words, uint64_t byte_offset, const std::vector<uint32_t>& piece);
    /** @brief Applies one copy region to a CPU model of the destination. */
    void ApplyRegion(const std::vector<uint32_t>& source, std::vector<uint32_t>& destination, const BufferCopyRegion& region);
    /** @brief Maps a host-visible buffer, copies out its words and unmaps it. */
    [[nodiscard]] std::vector<uint32_t> ReadHostVisibleWords(ResourceContext& context, const GraphicsResource& buffer, size_t size_bytes);
}

TEST_F(ResourceContextTest, DeviceLocalUploadPiecesLandAtTheirOffsets)
{
    constexpr uint64_t k_BufferBytes = 2048u;
    const std::vector<uint32_t> sentinel = MakeSentinelWords(k_BufferBytes / sizeof(uint32_t));
    const RhiBufferResourceData sentinelData{ std::as_bytes(std::span{ sentinel }), 0u };
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &sentinelData, 1u });

    // gaps of 360, 700 and 648 bytes lie between and after the pieces
    const std::vector<uint32_t> first = MakeWordPattern(10u, 11u);
    const std::vector<uint32_t> second = MakeWordPattern(25u, 12u);
    const std::vector<uint32_t> third = MakeWordPattern(50u, 13u);
    const std::array<RhiBufferResourceData, 3> pieces
    {
        RhiBufferResourceData{ std::as_bytes(std::span{ first }), 64u },
        RhiBufferResourceData{ std::as_bytes(std::span{ second }), 400u },
        RhiBufferResourceData{ std::as_bytes(std::span{ third }), 1200u },
    };

    rhi::OperationFuture setReply = context->SetBufferData(buffer, pieces, {});
    ASSERT_EQ(context->Wait(setReply, k_TimeoutNs), rhi::FutureState::Complete);

    std::vector<uint32_t> expected = sentinel;
    WritePiece(expected, 64u, first);
    WritePiece(expected, 400u, second);
    WritePiece(expected, 1200u, third);
    EXPECT_EQ(ReadBackWords(buffer, k_BufferBytes), expected);
}

TEST_F(ResourceContextTest, HostVisibleUploadPiecesLandAtTheirOffsets)
{
    constexpr uint64_t k_BufferBytes = 2048u;
    const std::vector<uint32_t> sentinel = MakeSentinelWords(k_BufferBytes / sizeof(uint32_t));
    const RhiBufferResourceData sentinelData{ std::as_bytes(std::span{ sentinel }), 0u };
    const GraphicsResource buffer = CreateBufferAndWait(HostVisibleBuffer(k_BufferBytes), std::span{ &sentinelData, 1u });

    const std::vector<uint32_t> first = MakeWordPattern(10u, 21u);
    const std::vector<uint32_t> second = MakeWordPattern(25u, 22u);
    const std::vector<uint32_t> third = MakeWordPattern(50u, 23u);
    const std::array<RhiBufferResourceData, 3> pieces
    {
        RhiBufferResourceData{ std::as_bytes(std::span{ first }), 64u },
        RhiBufferResourceData{ std::as_bytes(std::span{ second }), 400u },
        RhiBufferResourceData{ std::as_bytes(std::span{ third }), 1200u },
    };

    rhi::OperationFuture setReply = context->SetBufferData(buffer, pieces, {});
    ASSERT_EQ(context->Wait(setReply, k_TimeoutNs), rhi::FutureState::Complete);

    std::vector<uint32_t> expected = sentinel;
    WritePiece(expected, 64u, first);
    WritePiece(expected, 400u, second);
    WritePiece(expected, 1200u, third);
    EXPECT_EQ(ReadHostVisibleWords(*context, buffer, k_BufferBytes), expected);
}

TEST_F(ResourceContextTest, CopyBufferRegionsMovesFiveScatteredRanges)
{
    constexpr uint64_t k_BufferBytes = 4096u;
    const std::vector<uint32_t> sourceWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 31u);
    const std::vector<uint32_t> sentinel = MakeSentinelWords(k_BufferBytes / sizeof(uint32_t));
    const RhiBufferResourceData sourceData{ std::as_bytes(std::span{ sourceWords }), 0u };
    const RhiBufferResourceData sentinelData{ std::as_bytes(std::span{ sentinel }), 0u };
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &sourceData, 1u });
    const GraphicsResource destination = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &sentinelData, 1u });

    const std::array<BufferCopyRegion, 5> regions
    {
        BufferCopyRegion{ 0u, 3000u, 64u },
        BufferCopyRegion{ 128u, 0u, 256u },
        BufferCopyRegion{ 1024u, 2048u, 512u },
        BufferCopyRegion{ 2048u, 1024u, 400u },
        BufferCopyRegion{ 4000u, 4u, 96u },
    };

    rhi::OperationFuture copyReply = context->CopyBufferRegions(source, destination, regions, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    std::vector<uint32_t> expected = sentinel;
    for (const BufferCopyRegion& region : regions)
    {
        ApplyRegion(sourceWords, expected, region);
    }

    EXPECT_EQ(ReadBackWords(destination, k_BufferBytes), expected);
    EXPECT_EQ(ReadBackWords(source, k_BufferBytes), sourceWords);
}

TEST_F(ResourceContextTest, CopyBufferRegionsInsideOneBufferAcceptsRangesThatDoNotOverlap)
{
    constexpr uint64_t k_BufferBytes = 2048u;
    const std::vector<uint32_t> words = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 41u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &data, 1u });

    const std::array<BufferCopyRegion, 2> regions
    {
        BufferCopyRegion{ 0u, 1024u, 128u },
        BufferCopyRegion{ 256u, 1280u, 64u },
    };

    rhi::OperationFuture copyReply = context->CopyBufferRegions(buffer, buffer, regions, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    std::vector<uint32_t> expected = words;
    for (const BufferCopyRegion& region : regions)
    {
        ApplyRegion(words, expected, region);
    }

    EXPECT_EQ(ReadBackWords(buffer, k_BufferBytes), expected);
}

TEST_F(ResourceContextTest, CopyBufferRegionsInsideOneBufferRefusesRangesThatOverlap)
{
    constexpr uint64_t k_BufferBytes = 2048u;
    const std::vector<uint32_t> words = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 42u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &data, 1u });

    // the source and the destination of one region overlap
    const BufferCopyRegion selfOverlap{ 0u, 64u, 128u };
    rhi::OperationFuture selfReply = context->CopyBufferRegions(buffer, buffer, std::span{ &selfOverlap, 1u }, {});
    EXPECT_EQ(context->Wait(selfReply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(selfReply.Error(), rhi::ResourceError::InvalidRegion);

    // the destination of the first region is the source of the second
    const std::array<BufferCopyRegion, 2> crossOverlap
    {
        BufferCopyRegion{ 0u, 512u, 64u },
        BufferCopyRegion{ 512u, 1024u, 64u },
    };
    rhi::OperationFuture crossReply = context->CopyBufferRegions(buffer, buffer, crossOverlap, {});
    EXPECT_EQ(context->Wait(crossReply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(crossReply.Error(), rhi::ResourceError::InvalidRegion);

    // two destination ranges overlap
    const std::array<BufferCopyRegion, 2> destinationOverlap
    {
        BufferCopyRegion{ 0u, 512u, 64u },
        BufferCopyRegion{ 128u, 544u, 64u },
    };
    rhi::OperationFuture destinationReply = context->CopyBufferRegions(buffer, buffer, destinationOverlap, {});
    EXPECT_EQ(context->Wait(destinationReply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(destinationReply.Error(), rhi::ResourceError::InvalidRegion);

    EXPECT_EQ(ReadBackWords(buffer, k_BufferBytes), words);
}

TEST_F(ResourceContextTest, DirtyTileReadbackPacksScatteredTiles)
{
    constexpr uint64_t k_TileBytes = 1024u;
    constexpr int32_t k_TileCount = 1024;
    constexpr int32_t k_DirtyCount = 64;
    constexpr uint64_t k_PoolBytes = k_TileBytes * k_TileCount;
    constexpr uint64_t k_PackedBytes = k_TileBytes * k_DirtyCount;

    const std::vector<uint32_t> poolWords = MakeWordPattern(k_PoolBytes / sizeof(uint32_t), 51u);
    const RhiBufferResourceData poolData{ std::as_bytes(std::span{ poolWords }), 0u };
    const GraphicsResource pool = CreateBufferAndWait(DeviceLocalBuffer(k_PoolBytes), std::span{ &poolData, 1u });
    const GraphicsResource readback = CreateReadbackBuffer(k_PackedBytes);

    // 37 is odd, so the 64 tiles are distinct and scattered over the pool
    std::vector<BufferCopyRegion> regions;
    std::vector<uint32_t> expected(k_PackedBytes / sizeof(uint32_t));
    for (int32_t dirtyIndex = 0; dirtyIndex < k_DirtyCount; ++dirtyIndex)
    {
        const uint64_t tile = static_cast<uint64_t>((dirtyIndex * 37 + 5) % k_TileCount);
        const BufferCopyRegion region{ tile * k_TileBytes, static_cast<uint64_t>(dirtyIndex) * k_TileBytes, k_TileBytes };
        regions.push_back(region);
        ApplyRegion(poolWords, expected, region);
    }

    rhi::OperationFuture copyReply = context->CopyBufferRegions(pool, readback, regions, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    EXPECT_EQ(ReadWordsAndDestroy(readback, k_PackedBytes), expected);
}

TEST_F(ResourceContextTest, CopyBufferContentsIsTheOneRegionCase)
{
    const std::vector<uint32_t> smallWords = MakeWordPattern(64u, 61u);
    const std::vector<uint32_t> bigWords = MakeSentinelWords(128u);
    const RhiBufferResourceData smallData{ std::as_bytes(std::span{ smallWords }), 0u };
    const RhiBufferResourceData bigData{ std::as_bytes(std::span{ bigWords }), 0u };
    const GraphicsResource smaller = CreateBufferAndWait(DeviceLocalBuffer(256u), std::span{ &smallData, 1u });
    const GraphicsResource bigger = CreateBufferAndWait(DeviceLocalBuffer(512u), std::span{ &bigData, 1u });

    rhi::OperationFuture fitsReply = context->CopyBufferContents(smaller, bigger, {});
    ASSERT_EQ(context->Wait(fitsReply, k_TimeoutNs), rhi::FutureState::Complete);

    std::vector<uint32_t> expected = bigWords;
    WritePiece(expected, 0u, smallWords);
    EXPECT_EQ(ReadBackWords(bigger, 512u), expected);

    rhi::OperationFuture tooBigReply = context->CopyBufferContents(bigger, smaller, {});
    EXPECT_EQ(context->Wait(tooBigReply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(tooBigReply.Error(), rhi::ResourceError::CopySizeMismatch);
    EXPECT_EQ(ReadBackWords(smaller, 256u), smallWords);
}

TEST_F(ResourceContextTest, EveryInvalidRegionCauseFailsTheWholeCallAndChangesNothing)
{
    constexpr uint64_t k_BufferBytes = 256u;
    constexpr uint64_t k_FarOffset = std::numeric_limits<uint64_t>::max() - 3u;
    const std::vector<uint32_t> sourceWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 71u);
    const std::vector<uint32_t> destinationWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 72u);
    const RhiBufferResourceData sourceData{ std::as_bytes(std::span{ sourceWords }), 0u };
    const RhiBufferResourceData destinationData{ std::as_bytes(std::span{ destinationWords }), 0u };
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &sourceData, 1u });
    const GraphicsResource destination = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &destinationData, 1u });

    const auto expectRejected = [&](std::string_view name, rhi::OperationFuture future)
    {
        SCOPED_TRACE(std::string{ name });
        EXPECT_EQ(context->Wait(future, k_TimeoutNs), rhi::FutureState::Failed);
        EXPECT_EQ(future.Error(), rhi::ResourceError::InvalidRegion);
        EXPECT_EQ(ReadBackWords(destination, k_BufferBytes), destinationWords);
        EXPECT_EQ(ReadBackWords(source, k_BufferBytes), sourceWords);
    };

    const auto copyOne = [&](BufferCopyRegion region)
    {
        return context->CopyBufferRegions(source, destination, std::span{ &region, 1u }, {});
    };

    expectRejected("size 0", copyOne(BufferCopyRegion{ 0u, 0u, 0u }));
    expectRejected("source range past the end", copyOne(BufferCopyRegion{ 128u, 0u, 256u }));
    expectRejected("destination range past the end", copyOne(BufferCopyRegion{ 0u, 128u, 256u }));
    expectRejected("source offset that overflows", copyOne(BufferCopyRegion{ k_FarOffset, 0u, 8u }));
    expectRejected("destination offset that overflows", copyOne(BufferCopyRegion{ 0u, k_FarOffset, 8u }));
    expectRejected("size that overflows", copyOne(BufferCopyRegion{ 4u, 4u, k_FarOffset }));
    expectRejected("source offset not a multiple of 4", copyOne(BufferCopyRegion{ 2u, 0u, 8u }));
    expectRejected("destination offset not a multiple of 4", copyOne(BufferCopyRegion{ 0u, 2u, 8u }));
    expectRejected("size not a multiple of 4", copyOne(BufferCopyRegion{ 0u, 0u, 6u }));

    // one good region and one bad region: the good one must not happen either
    const std::array<BufferCopyRegion, 2> oneBadRegion
    {
        BufferCopyRegion{ 0u, 0u, 64u },
        BufferCopyRegion{ 0u, 200u, 64u },
    };
    expectRejected("one bad region among good ones", context->CopyBufferRegions(source, destination, oneBadRegion, {}));

    const std::vector<BufferCopyRegion> tooMany(static_cast<size_t>(k_MaxTransferRegions) + 1u, BufferCopyRegion{ 0u, 0u, 4u });
    expectRejected("257 regions", context->CopyBufferRegions(source, destination, tooMany, {}));
    expectRejected("no regions", context->CopyBufferRegions(source, destination, std::span<const BufferCopyRegion>{}, {}));

    const std::vector<uint32_t> overlapFirst = MakeWordPattern(16u, 81u);
    const std::vector<uint32_t> overlapSecond = MakeWordPattern(16u, 82u);
    const std::array<RhiBufferResourceData, 2> overlappingPieces
    {
        RhiBufferResourceData{ std::as_bytes(std::span{ overlapFirst }), 0u },
        RhiBufferResourceData{ std::as_bytes(std::span{ overlapSecond }), 32u },
    };
    expectRejected("overlapping upload pieces", context->SetBufferData(destination, overlappingPieces, {}));

    const RhiBufferResourceData pastTheEnd{ std::as_bytes(std::span{ overlapFirst }), 224u };
    expectRejected("upload piece past the end", context->SetBufferData(destination, std::span{ &pastTheEnd, 1u }, {}));

    const RhiBufferResourceData overflowing{ std::as_bytes(std::span{ overlapFirst }), k_FarOffset };
    expectRejected("upload offset that overflows", context->SetBufferData(destination, std::span{ &overflowing, 1u }, {}));

    const RhiBufferResourceData misaligned{ std::as_bytes(std::span{ overlapFirst }), 2u };
    expectRejected("upload offset not a multiple of 4", context->SetBufferData(destination, std::span{ &misaligned, 1u }, {}));

    const std::array<std::byte, 6> sixBytes{};
    const RhiBufferResourceData oddSize{ std::span<const std::byte>{ sixBytes }, 0u };
    expectRejected("upload piece size not a multiple of 4", context->SetBufferData(destination, std::span{ &oddSize, 1u }, {}));
}

TEST_F(ResourceContextTest, ManyRegionsShareOneTicket)
{
    constexpr uint64_t k_RegionBytes = 16u;
    constexpr uint64_t k_BufferBytes = k_RegionBytes * k_MaxTransferRegions;
    const std::vector<uint32_t> sourceWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 91u);
    const std::vector<uint32_t> sentinel = MakeSentinelWords(k_BufferBytes / sizeof(uint32_t));
    const RhiBufferResourceData sourceData{ std::as_bytes(std::span{ sourceWords }), 0u };
    const RhiBufferResourceData sentinelData{ std::as_bytes(std::span{ sentinel }), 0u };
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &sourceData, 1u });
    const GraphicsResource destination = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &sentinelData, 1u });

    // a probe transfer gives the ticket just before the one that the copy gets
    rhi::OperationFuture probeReply = context->FillBuffer(destination, k_SentinelWord, 0u, k_BufferBytes, {});
    ASSERT_EQ(context->Wait(probeReply, k_TimeoutNs), rhi::FutureState::Complete);
    const uint64_t probeTicket = probeReply.Completion().Value;
    const int64_t recordedBefore = context->Counters().TransfersRecorded;

    // the destination order is reversed, so that every region moves data
    std::vector<BufferCopyRegion> regions;
    std::vector<uint32_t> expected = sentinel;
    for (int32_t index = 0; index < k_MaxTransferRegions; ++index)
    {
        const uint64_t sourceOffset = static_cast<uint64_t>(index) * k_RegionBytes;
        const uint64_t destinationOffset = static_cast<uint64_t>(k_MaxTransferRegions - 1 - index) * k_RegionBytes;
        const BufferCopyRegion region{ sourceOffset, destinationOffset, k_RegionBytes };
        regions.push_back(region);
        ApplyRegion(sourceWords, expected, region);
    }

    rhi::OperationFuture copyReply = context->CopyBufferRegions(source, destination, regions, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    EXPECT_EQ(copyReply.Completion().Value, probeTicket + 1u);
    EXPECT_EQ(context->Counters().TransfersRecorded - recordedBefore, 1);
    EXPECT_EQ(ReadBackWords(destination, k_BufferBytes), expected);
}

namespace
{
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
        info.Usage = BufferUsageBits::ComputeShaderUBO;
        info.Size = size_bytes;
        return info;
    }

    std::vector<uint32_t> MakeSentinelWords(size_t count)
    {
        return std::vector<uint32_t>(count, k_SentinelWord);
    }

    void WritePiece(std::vector<uint32_t>& words, uint64_t byte_offset, const std::vector<uint32_t>& piece)
    {
        std::memcpy(words.data() + byte_offset / sizeof(uint32_t), piece.data(), piece.size() * sizeof(uint32_t));
    }

    void ApplyRegion(const std::vector<uint32_t>& source, std::vector<uint32_t>& destination, const BufferCopyRegion& region)
    {
        const uint32_t* sourceBytes = source.data() + region.SourceOffset / sizeof(uint32_t);
        std::memcpy(destination.data() + region.DestinationOffset / sizeof(uint32_t), sourceBytes, region.Size);
    }

    std::vector<uint32_t> ReadHostVisibleWords(ResourceContext& context, const GraphicsResource& buffer, size_t size_bytes)
    {
        std::vector<uint32_t> words(size_bytes / sizeof(uint32_t));

        rhi::ResourceFuture<void*> mapReply = context.MapBuffer(buffer, size_bytes, 0u);
        EXPECT_EQ(context.Wait(mapReply, k_TimeoutNs), rhi::FutureState::Complete);
        if (mapReply.Get() != nullptr)
        {
            std::memcpy(words.data(), mapReply.Get(), size_bytes);
        }

        rhi::OperationFuture unmapReply = context.UnmapBuffer(buffer, size_bytes, 0u);
        EXPECT_EQ(context.Wait(unmapReply, k_TimeoutNs), rhi::FutureState::Complete);

        return words;
    }
}
