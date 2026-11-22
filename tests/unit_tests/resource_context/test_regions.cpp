#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "Device.hpp"
#include "ImageDataFormats.hpp"
#include "RhiSystem.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "RhiResourceFlags.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

    /** @brief A box of texels in one mip level. */
    struct TexelBox
    {
        int32_t X{ 0 };
        int32_t Y{ 0 };
        int32_t Width{ 0 };
        int32_t Height{ 0 };
    };

    /** @brief A CPU copy of a square RGBA8 2D array image, one word per texel, for each mip level and layer. */
    struct ImageModel
    {
        int32_t Size{ 0 };
        int32_t LayerCount{ 0 };
        int32_t MipCount{ 0 };
        // the index is mip level * LayerCount + layer
        std::vector<std::vector<uint32_t>> Subresources;
    };

    constexpr rhi::ImageUsageTransition k_Resting{ ImageUsageBits::SampledImage, ImageUsageBits::SampledImage };

    [[nodiscard]] ImageModel MakeImageModel(int32_t size, int32_t layer_count, int32_t mip_count, uint32_t seed);
    [[nodiscard]] int32_t MipEdge(const ImageModel& model, int32_t mip_level);
    [[nodiscard]] std::vector<uint32_t>& SubresourceOf(ImageModel& model, int32_t mip_level, int32_t layer);
    [[nodiscard]] const std::vector<uint32_t>& SubresourceOf(const ImageModel& model, int32_t mip_level, int32_t layer);
    [[nodiscard]] std::vector<uint32_t> CropBox(const ImageModel& model, int32_t mip_level, int32_t layer, const TexelBox& box);
    void WriteBox(ImageModel& model, int32_t mip_level, int32_t layer, const TexelBox& box, std::span<const uint32_t> texels);
    [[nodiscard]] ImageCreateInfo RgbaImageInfo(const ImageModel& model);
    [[nodiscard]] ImageRegion MakeRegion(int32_t mip_level, int32_t base_layer, int32_t layer_count, const TexelBox& box);
    [[nodiscard]] BufferImageRegion MakeBufferImageRegion(uint64_t buffer_offset, int32_t row_texels, int32_t image_rows, const ImageRegion& image);
    /** @brief Returns one piece for each mip level and layer, with the words of the model. */
    [[nodiscard]] std::vector<RhiImageResourceData> MakeFullUpload(const ImageModel& model);
    /** @brief Returns one region for each mip level and layer, tightly packed one after another from offset 0. */
    [[nodiscard]] std::vector<BufferImageRegion> MakeWholeReadbackRegions(const ImageModel& model);
    [[nodiscard]] uint64_t WholeReadbackBytes(const ImageModel& model);
    [[nodiscard]] RhiImageResourceData MakePiece(std::span<const uint32_t> texels, int32_t mip_level, int32_t layer, int32_t layer_count, const TexelBox& box);
    /** @brief Returns how many words differ. The spans must have the same size. */
    [[nodiscard]] int32_t CountMismatchedWords(std::span<const uint32_t> actual, std::span<const uint32_t> expected);
    /** @brief Writes the texels of one box into a CPU model of a buffer. The layers of the box are `image_rows * row_texels`
     *  words apart, as in a Vulkan buffer image copy. */
    void PlaceBoxInBuffer(std::vector<uint32_t>& words, uint64_t buffer_offset, int32_t row_texels, int32_t image_rows, int32_t layer_slot, int32_t box_width, int32_t box_height, std::span<const uint32_t> texels);
}

/** @brief The fixture of the image region tests: it fills an image from a CPU model, and reads it back to compare. */
class ImageRegionTest : public ResourceContextTest
{
protected:
    /** @brief Creates the image of the model with every texel uploaded, in the resting usage. */
    [[nodiscard]] GraphicsResource CreateFilledImage(const ImageModel& model);
    /** @brief Reads every mip level and layer back, and compares each with the model. */
    void ExpectImageMatches(const GraphicsResource& image, rhi::ImageUsageTransition usage, const ImageModel& model);
    void ExpectInvalidRegion(std::string_view name, const rhi::OperationFuture& future);
};

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

TEST_F(ImageRegionTest, UploadRectangleChangesOnlyItsTexels)
{
    ImageModel model = MakeImageModel(64, 4, 7, 11u);
    const GraphicsResource image = CreateFilledImage(model);
    ExpectImageMatches(image, k_Resting, model);

    // a rectangle inside mip 1 of layer 2
    const TexelBox box{ 5, 7, 10, 6 };
    const std::vector<uint32_t> texels = MakeWordPattern(60u, 12345u);
    const RhiImageResourceData piece = MakePiece(texels, 1, 2, 1, box);
    rhi::OperationFuture reply = context->SetImageData(image, k_Resting, std::span{ &piece, 1u }, {});
    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    WriteBox(model, 1, 2, box, texels);
    ExpectImageMatches(image, k_Resting, model);

    // one piece for two layers: the layers follow each other in the data
    const TexelBox layerBox{ 1, 2, 9, 3 };
    const std::vector<uint32_t> twoLayers = MakeWordPattern(54u, 54321u);
    const RhiImageResourceData layerPiece = MakePiece(twoLayers, 0, 0, 2, layerBox);
    rhi::OperationFuture layerReply = context->SetImageData(image, k_Resting, std::span{ &layerPiece, 1u }, {});
    ASSERT_EQ(context->Wait(layerReply, k_TimeoutNs), rhi::FutureState::Complete);
    WriteBox(model, 0, 0, layerBox, std::span<const uint32_t>(twoLayers).first(27u));
    WriteBox(model, 0, 1, layerBox, std::span<const uint32_t>(twoLayers).subspan(27u, 27u));
    ExpectImageMatches(image, k_Resting, model);
}

TEST_F(ImageRegionTest, ReadbackRegionsMatchTheCpuCrop)
{
    const ImageModel model = MakeImageModel(64, 4, 7, 21u);
    const GraphicsResource image = CreateFilledImage(model);

    constexpr uint64_t k_BufferBytes = 12288u;
    const TexelBox firstBox{ 3, 5, 20, 9 };
    const TexelBox secondBox{ 2, 2, 8, 8 };
    const TexelBox thirdBox{ 0, 0, 32, 32 };
    const TexelBox fourthBox{ 0, 0, 16, 16 };
    const std::array<BufferImageRegion, 4> regions
    {
        // the row length is larger than the width
        MakeBufferImageRegion(0u, 32, 0, MakeRegion(0, 1, 1, firstBox)),
        MakeBufferImageRegion(2048u, 0, 0, MakeRegion(2, 3, 1, secondBox)),
        MakeBufferImageRegion(4096u, 0, 0, MakeRegion(1, 0, 1, thirdBox)),
        // two layers, with an image height larger than the box
        MakeBufferImageRegion(8192u, 0, 20, MakeRegion(2, 1, 2, fourthBox)),
    };

    const GraphicsResource readback = CreateReadbackBuffer(k_BufferBytes);
    rhi::OperationFuture fillReply = context->FillBuffer(readback, k_SentinelWord, 0u, k_BufferBytes, {});
    ASSERT_EQ(context->Wait(fillReply, k_TimeoutNs), rhi::FutureState::Complete);
    rhi::OperationFuture copyReply = context->CopyImageToBufferRegions(image, k_Resting, readback, regions, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    std::vector<uint32_t> expected = MakeSentinelWords(k_BufferBytes / sizeof(uint32_t));
    PlaceBoxInBuffer(expected, 0u, 32, 0, 0, 20, 9, CropBox(model, 0, 1, firstBox));
    PlaceBoxInBuffer(expected, 2048u, 0, 0, 0, 8, 8, CropBox(model, 2, 3, secondBox));
    PlaceBoxInBuffer(expected, 4096u, 0, 0, 0, 32, 32, CropBox(model, 1, 0, thirdBox));
    PlaceBoxInBuffer(expected, 8192u, 0, 20, 0, 16, 16, CropBox(model, 2, 1, fourthBox));
    PlaceBoxInBuffer(expected, 8192u, 0, 20, 1, 16, 16, CropBox(model, 2, 2, fourthBox));

    const std::vector<uint32_t> actual = ReadWordsAndDestroy(readback, k_BufferBytes);
    EXPECT_EQ(CountMismatchedWords(actual, expected), 0);

    // the whole-resource readback is the second opinion on mip 0 of every layer
    std::vector<uint32_t> mipZeroOfEveryLayer;
    for (int32_t layer = 0; layer < model.LayerCount; ++layer)
    {
        const std::vector<uint32_t>& layerWords = SubresourceOf(model, 0, layer);
        mipZeroOfEveryLayer.insert(mipZeroOfEveryLayer.end(), layerWords.begin(), layerWords.end());
    }

    const std::vector<uint32_t> whole = ReadBackImageWords(image, k_Resting, mipZeroOfEveryLayer.size() * sizeof(uint32_t));
    EXPECT_EQ(CountMismatchedWords(whole, mipZeroOfEveryLayer), 0);
}

TEST_F(ImageRegionTest, BufferToImageRegionsScatterRectangles)
{
    ImageModel model = MakeImageModel(64, 4, 7, 31u);
    const GraphicsResource image = CreateFilledImage(model);

    constexpr uint64_t k_BufferBytes = 16384u;
    const std::vector<uint32_t> sourceWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 32u);
    const RhiBufferResourceData sourceData{ std::as_bytes(std::span{ sourceWords }), 0u };
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &sourceData, 1u });

    const TexelBox firstBox{ 10, 10, 12, 6 };
    const TexelBox secondBox{ 0, 0, 8, 8 };
    const TexelBox thirdBox{ 4, 4, 16, 16 };
    const std::array<BufferImageRegion, 3> regions
    {
        MakeBufferImageRegion(0u, 16, 0, MakeRegion(0, 0, 1, firstBox)),
        MakeBufferImageRegion(1024u, 0, 0, MakeRegion(3, 2, 1, secondBox)),
        MakeBufferImageRegion(2048u, 20, 24, MakeRegion(1, 2, 2, thirdBox)),
    };

    rhi::OperationFuture copyReply = context->CopyBufferToImageRegions(source, image, k_Resting, regions, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    const auto cropOfSource = [&](uint64_t buffer_offset, int32_t row_texels, int32_t layer_stride_words, int32_t layer_slot, const TexelBox& box)
    {
        std::vector<uint32_t> texels;
        const size_t firstWord = static_cast<size_t>(buffer_offset / sizeof(uint32_t)) + static_cast<size_t>(layer_slot * layer_stride_words);
        for (int32_t row = 0; row < box.Height; ++row)
        {
            for (int32_t column = 0; column < box.Width; ++column)
            {
                texels.push_back(sourceWords[firstWord + static_cast<size_t>(row * row_texels + column)]);
            }
        }

        return texels;
    };

    WriteBox(model, 0, 0, firstBox, cropOfSource(0u, 16, 0, 0, firstBox));
    WriteBox(model, 3, 2, secondBox, cropOfSource(1024u, 8, 0, 0, secondBox));
    WriteBox(model, 1, 2, thirdBox, cropOfSource(2048u, 20, 24 * 20, 0, thirdBox));
    WriteBox(model, 1, 3, thirdBox, cropOfSource(2048u, 20, 24 * 20, 1, thirdBox));
    ExpectImageMatches(image, k_Resting, model);
}

TEST_F(ImageRegionTest, CopyImageRegionsMovesBoxesBetweenImages)
{
    const ImageModel sourceModel = MakeImageModel(64, 4, 7, 41u);
    ImageModel destinationModel = MakeImageModel(64, 4, 7, 42u);
    const GraphicsResource source = CreateFilledImage(sourceModel);
    const GraphicsResource destination = CreateFilledImage(destinationModel);

    const TexelBox firstBox{ 4, 4, 16, 8 };
    const TexelBox secondBox{ 0, 0, 32, 32 };
    const TexelBox thirdBox{ 2, 2, 8, 8 };
    const std::array<ImageCopyRegion, 3> regions
    {
        ImageCopyRegion{ MakeRegion(0, 0, 1, firstBox), MakeRegion(0, 1, 1, TexelBox{ 30, 40, 0, 0 }) },
        ImageCopyRegion{ MakeRegion(1, 2, 1, secondBox), MakeRegion(1, 3, 1, TexelBox{ 0, 0, 0, 0 }) },
        ImageCopyRegion{ MakeRegion(2, 0, 2, thirdBox), MakeRegion(3, 2, 2, TexelBox{ 0, 0, 0, 0 }) },
    };

    rhi::OperationFuture copyReply = context->CopyImageRegions(source, k_Resting, destination, k_Resting, regions, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    WriteBox(destinationModel, 0, 1, TexelBox{ 30, 40, 16, 8 }, CropBox(sourceModel, 0, 0, firstBox));
    WriteBox(destinationModel, 1, 3, TexelBox{ 0, 0, 32, 32 }, CropBox(sourceModel, 1, 2, secondBox));
    WriteBox(destinationModel, 3, 2, TexelBox{ 0, 0, 8, 8 }, CropBox(sourceModel, 2, 0, thirdBox));
    WriteBox(destinationModel, 3, 3, TexelBox{ 0, 0, 8, 8 }, CropBox(sourceModel, 2, 1, thirdBox));
    ExpectImageMatches(destination, k_Resting, destinationModel);
    ExpectImageMatches(source, k_Resting, sourceModel);
}

TEST_F(ImageRegionTest, CopyImageRegionsInsideOneImage)
{
    ImageModel model = MakeImageModel(32, 4, 3, 43u);
    const GraphicsResource image = CreateFilledImage(model);
    const ImageModel before = model;

    const TexelBox wholeMip0{ 0, 0, 32, 32 };
    const TexelBox cornerBox{ 0, 0, 8, 8 };
    const std::array<ImageCopyRegion, 2> regions
    {
        ImageCopyRegion{ MakeRegion(0, 0, 1, wholeMip0), MakeRegion(0, 1, 1, TexelBox{ 0, 0, 0, 0 }) },
        ImageCopyRegion{ MakeRegion(1, 2, 1, cornerBox), MakeRegion(1, 2, 1, TexelBox{ 8, 8, 0, 0 }) },
    };

    rhi::OperationFuture copyReply = context->CopyImageRegions(image, k_Resting, image, k_Resting, regions, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    WriteBox(model, 0, 1, wholeMip0, CropBox(before, 0, 0, wholeMip0));
    WriteBox(model, 1, 2, TexelBox{ 8, 8, 8, 8 }, CropBox(before, 1, 2, cornerBox));
    ExpectImageMatches(image, k_Resting, model);

    // the copy reads and writes one layout, so the two usages must be equal
    const rhi::ImageUsageTransition otherUsage{ ImageUsageBits::ComputeShaderSRV, ImageUsageBits::SampledImage };
    rhi::OperationFuture unequalReply = context->CopyImageRegions(image, k_Resting, image, otherUsage, regions, {});
    ExpectInvalidRegion("unequal usages for one image", unequalReply);

    // the destination of the first region is the source of the second
    const std::array<ImageCopyRegion, 2> chained
    {
        ImageCopyRegion{ MakeRegion(0, 0, 1, cornerBox), MakeRegion(0, 1, 1, TexelBox{ 0, 0, 0, 0 }) },
        ImageCopyRegion{ MakeRegion(0, 1, 1, cornerBox), MakeRegion(0, 2, 1, TexelBox{ 0, 0, 0, 0 }) },
    };
    rhi::OperationFuture chainedReply = context->CopyImageRegions(image, k_Resting, image, k_Resting, chained, {});
    ExpectInvalidRegion("a source box that overlaps a destination box", chainedReply);

    const std::array<ImageCopyRegion, 1> inPlace
    {
        ImageCopyRegion{ MakeRegion(0, 0, 1, cornerBox), MakeRegion(0, 0, 1, TexelBox{ 4, 4, 0, 0 }) },
    };
    rhi::OperationFuture inPlaceReply = context->CopyImageRegions(image, k_Resting, image, k_Resting, inPlace, {});
    ExpectInvalidRegion("a box that overlaps itself", inPlaceReply);

    ExpectImageMatches(image, k_Resting, model);
}

TEST_F(ImageRegionTest, BarriersTouchOnlyTheMipLevelsThatAreWritten)
{
    ImageModel model = MakeImageModel(32, 1, 2, 51u);
    const ImageCreateInfo info = RgbaImageInfo(model);

    // mip 0 is uploaded in the shader layout, so mip 1 is still UNDEFINED
    const std::vector<RhiImageResourceData> fullUpload = MakeFullUpload(model);
    rhi::ResourceFuture<GraphicsResource> createReply = context->CreateImage(info, std::span{ fullUpload }.first(1u), ImageUsageBits::ComputeShaderSRV);
    ASSERT_EQ(context->Wait(createReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = createReply.Get();

    // mip 1 is written from UNDEFINED, and left in the transfer source layout
    const rhi::ImageUsageTransition discardToSource{ ImageUsageBits::Discard, ImageUsageBits::TransferSource };
    rhi::OperationFuture firstReply = context->SetImageData(image, discardToSource, std::span{ fullUpload }.subspan(1u, 1u), {});
    ASSERT_EQ(context->Wait(firstReply, k_TimeoutNs), rhi::FutureState::Complete);

    // only mip 1 changes. a transition of the whole image would name mip 0 with the wrong old layout
    const rhi::ImageUsageTransition sourceToSource{ ImageUsageBits::TransferSource, ImageUsageBits::TransferSource };
    const TexelBox box{ 4, 4, 8, 8 };
    const std::vector<uint32_t> texels = MakeWordPattern(64u, 52u);
    const RhiImageResourceData piece = MakePiece(texels, 1, 0, 1, box);
    rhi::OperationFuture secondReply = context->SetImageData(image, sourceToSource, std::span{ &piece, 1u }, {});
    ASSERT_EQ(context->Wait(secondReply, k_TimeoutNs), rhi::FutureState::Complete);
    WriteBox(model, 1, 0, box, texels);

    // read each mip level in the layout that it has
    const std::vector<BufferImageRegion> allRegions = MakeWholeReadbackRegions(model);
    const std::span<const BufferImageRegion> mipZero = std::span{ allRegions }.first(1u);
    const std::span<const BufferImageRegion> mipOne = std::span{ allRegions }.subspan(1u, 1u);
    const rhi::ImageUsageTransition shaderToShader{ ImageUsageBits::ComputeShaderSRV, ImageUsageBits::ComputeShaderSRV };

    const GraphicsResource mipZeroReadback = CreateReadbackBuffer(WholeReadbackBytes(model));
    rhi::OperationFuture zeroReply = context->CopyImageToBufferRegions(image, shaderToShader, mipZeroReadback, mipZero, {});
    ASSERT_EQ(context->Wait(zeroReply, k_TimeoutNs), rhi::FutureState::Complete);
    const std::vector<uint32_t> zeroWords = ReadWordsAndDestroy(mipZeroReadback, WholeReadbackBytes(model));
    EXPECT_EQ(CountMismatchedWords(std::span{ zeroWords }.first(SubresourceOf(model, 0, 0).size()), SubresourceOf(model, 0, 0)), 0);

    const GraphicsResource mipOneReadback = CreateReadbackBuffer(WholeReadbackBytes(model));
    std::array<BufferImageRegion, 1> mipOneAtZero{ mipOne[0] };
    mipOneAtZero[0].BufferOffset = 0u;
    rhi::OperationFuture oneReply = context->CopyImageToBufferRegions(image, sourceToSource, mipOneReadback, mipOneAtZero, {});
    ASSERT_EQ(context->Wait(oneReply, k_TimeoutNs), rhi::FutureState::Complete);
    const std::vector<uint32_t> oneWords = ReadWordsAndDestroy(mipOneReadback, WholeReadbackBytes(model));
    EXPECT_EQ(CountMismatchedWords(std::span{ oneWords }.first(SubresourceOf(model, 1, 0).size()), SubresourceOf(model, 1, 0)), 0);
}

TEST_F(ImageRegionTest, DiscardNeedsEveryBoxToCoverItsWholeMipLevel)
{
    ImageModel model = MakeImageModel(32, 2, 2, 61u);
    const GraphicsResource image = CreateFilledImage(model);
    const rhi::ImageUsageTransition discardToSampled{ ImageUsageBits::Discard, ImageUsageBits::SampledImage };

    // a piece that covers mip 1 of both layers is accepted
    const std::vector<uint32_t> newWords = MakeWordPattern(2u * 16u * 16u, 62u);
    const TexelBox wholeMip1{ 0, 0, 16, 16 };
    const RhiImageResourceData wholePiece = MakePiece(newWords, 1, 0, 2, wholeMip1);
    rhi::OperationFuture wholeReply = context->SetImageData(image, discardToSampled, std::span{ &wholePiece, 1u }, {});
    ASSERT_EQ(context->Wait(wholeReply, k_TimeoutNs), rhi::FutureState::Complete);
    WriteBox(model, 1, 0, wholeMip1, std::span<const uint32_t>(newWords).first(256u));
    WriteBox(model, 1, 1, wholeMip1, std::span<const uint32_t>(newWords).subspan(256u, 256u));
    ExpectImageMatches(image, k_Resting, model);

    // a rectangle is refused, and nothing changes
    const std::vector<uint32_t> partWords = MakeWordPattern(64u, 63u);
    const RhiImageResourceData partPiece = MakePiece(partWords, 1, 0, 1, TexelBox{ 0, 0, 8, 8 });
    rhi::OperationFuture partReply = context->SetImageData(image, discardToSampled, std::span{ &partPiece, 1u }, {});
    ExpectInvalidRegion("a partial upload that discards", partReply);

    // the same two rules for the buffer to image copy and the image to image copy
    const std::vector<uint32_t> sourceWords = MakeWordPattern(1024u, 64u);
    const RhiBufferResourceData sourceData{ std::as_bytes(std::span{ sourceWords }), 0u };
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalBuffer(4096u), std::span{ &sourceData, 1u });

    const BufferImageRegion partRegion = MakeBufferImageRegion(0u, 0, 0, MakeRegion(1, 0, 1, TexelBox{ 0, 0, 8, 8 }));
    rhi::OperationFuture partCopyReply = context->CopyBufferToImageRegions(source, image, discardToSampled, std::span{ &partRegion, 1u }, {});
    ExpectInvalidRegion("a partial buffer to image copy that discards", partCopyReply);

    const ImageModel otherModel = MakeImageModel(32, 2, 2, 65u);
    const GraphicsResource other = CreateFilledImage(otherModel);
    const ImageCopyRegion partImageRegion{ MakeRegion(1, 0, 1, TexelBox{ 0, 0, 8, 8 }), MakeRegion(1, 0, 1, TexelBox{ 0, 0, 0, 0 }) };
    rhi::OperationFuture partImageReply = context->CopyImageRegions(other, k_Resting, image, discardToSampled, std::span{ &partImageRegion, 1u }, {});
    ExpectInvalidRegion("a partial image copy that discards", partImageReply);
    ExpectImageMatches(image, k_Resting, model);

    const BufferImageRegion wholeRegion = MakeBufferImageRegion(0u, 0, 0, MakeRegion(1, 0, 1, wholeMip1));
    rhi::OperationFuture wholeCopyReply = context->CopyBufferToImageRegions(source, image, discardToSampled, std::span{ &wholeRegion, 1u }, {});
    ASSERT_EQ(context->Wait(wholeCopyReply, k_TimeoutNs), rhi::FutureState::Complete);
    WriteBox(model, 1, 0, wholeMip1, std::span<const uint32_t>(sourceWords).first(256u));
    ExpectImageMatches(image, k_Resting, model);

    const ImageCopyRegion wholeImageRegion{ MakeRegion(1, 1, 1, wholeMip1), MakeRegion(1, 1, 1, TexelBox{ 0, 0, 0, 0 }) };
    rhi::OperationFuture wholeImageReply = context->CopyImageRegions(other, k_Resting, image, discardToSampled, std::span{ &wholeImageRegion, 1u }, {});
    ASSERT_EQ(context->Wait(wholeImageReply, k_TimeoutNs), rhi::FutureState::Complete);
    WriteBox(model, 1, 1, wholeMip1, CropBox(otherModel, 1, 1, wholeMip1));
    ExpectImageMatches(image, k_Resting, model);
}

TEST_F(ImageRegionTest, BlockCompressedBoxesNeedBlockEdges)
{
    if (!rhiSystem->GetDevice()->IsFeatureEnabled("VkPhysicalDeviceFeatures.textureCompressionBC"))
    {
        GTEST_SKIP() << "the device does not enable textureCompressionBC, so a BC7 image cannot be a transfer target";
    }

    // 16 by 16 texels are 4 by 4 blocks, and a BC7 block is 4 words
    constexpr int32_t k_BlockWords = 4;
    constexpr int32_t k_BlocksPerEdge = 4;
    std::vector<uint32_t> blocks = MakeWordPattern(k_BlocksPerEdge * k_BlocksPerEdge * k_BlockWords, 71u);

    ImageCreateInfo info{};
    info.Domain = ResourceDomain::GPUOnly;
    info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::TransferSource | ImageUsageBits::TransferDestination;
    info.Type = ImageType::Type2D;
    info.Format = ImageFormat{ ImageComponentFormats::BC7, ImageDataType::UNorm };
    info.Width = 16u;
    info.Height = 16u;
    info.Depth = 1u;

    RhiImageResourceData whole{};
    whole.Data = std::as_bytes(std::span{ blocks });
    whole.Width = 16;
    whole.Height = 16;
    rhi::ResourceFuture<GraphicsResource> createReply = context->CreateImage(info, std::span{ &whole, 1u }, ImageUsageBits::SampledImage);
    ASSERT_EQ(context->Wait(createReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource image = createReply.Get();

    const BufferImageRegion wholeRegion = MakeBufferImageRegion(0u, 0, 0, MakeRegion(0, 0, 1, TexelBox{ 0, 0, 16, 16 }));
    const auto expectImage = [&]()
    {
        constexpr uint64_t k_Bytes = 256u;
        const GraphicsResource readback = CreateReadbackBuffer(k_Bytes);
        rhi::OperationFuture reply = context->CopyImageToBufferRegions(image, k_Resting, readback, std::span{ &wholeRegion, 1u }, {});
        EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
        EXPECT_EQ(CountMismatchedWords(ReadWordsAndDestroy(readback, k_Bytes), blocks), 0);
    };
    expectImage();

    // a box that starts off a block edge, and a box that ends off one, inside the mip level
    const std::vector<uint32_t> smallBlock = MakeWordPattern(8u, 72u);
    const RhiImageResourceData startsOff = MakePiece(smallBlock, 0, 0, 1, TexelBox{ 2, 0, 4, 4 });
    ExpectInvalidRegion("a block box that starts off a block edge", context->SetImageData(image, k_Resting, std::span{ &startsOff, 1u }, {}));
    const RhiImageResourceData endsOff = MakePiece(smallBlock, 0, 0, 1, TexelBox{ 4, 4, 6, 4 });
    ExpectInvalidRegion("a block box that ends off a block edge", context->SetImageData(image, k_Resting, std::span{ &endsOff, 1u }, {}));
    const BufferImageRegion offEdgeRegion = MakeBufferImageRegion(0u, 0, 0, MakeRegion(0, 0, 1, TexelBox{ 0, 2, 4, 4 }));
    const GraphicsResource scratch = CreateReadbackBuffer(256u);
    ExpectInvalidRegion("a block readback off a block edge", context->CopyImageToBufferRegions(image, k_Resting, scratch, std::span{ &offEdgeRegion, 1u }, {}));
    DestroyAndWait(scratch);
    expectImage();

    // a box on block edges: 2 by 2 blocks, at block (1, 1)
    const std::vector<uint32_t> fourBlocks = MakeWordPattern(4u * k_BlockWords, 73u);
    const RhiImageResourceData onEdges = MakePiece(fourBlocks, 0, 0, 1, TexelBox{ 4, 4, 8, 8 });
    rhi::OperationFuture setReply = context->SetImageData(image, k_Resting, std::span{ &onEdges, 1u }, {});
    ASSERT_EQ(context->Wait(setReply, k_TimeoutNs), rhi::FutureState::Complete);

    for (int32_t row = 0; row < 2; ++row)
    {
        for (int32_t column = 0; column < 2; ++column)
        {
            const size_t blockIndex = static_cast<size_t>((1 + row) * k_BlocksPerEdge + 1 + column);
            const size_t pieceIndex = static_cast<size_t>(row * 2 + column);
            std::copy_n(fourBlocks.begin() + static_cast<std::ptrdiff_t>(pieceIndex * k_BlockWords), k_BlockWords, blocks.begin() + static_cast<std::ptrdiff_t>(blockIndex * k_BlockWords));
        }
    }

    expectImage();
}

TEST_F(ImageRegionTest, EveryInvalidImageRegionCauseFailsTheWholeCallAndChangesNothing)
{
    const ImageModel model = MakeImageModel(16, 2, 3, 81u);
    const GraphicsResource image = CreateFilledImage(model);
    const ImageModel otherModel = MakeImageModel(16, 2, 3, 82u);
    const GraphicsResource other = CreateFilledImage(otherModel);

    constexpr uint64_t k_BufferBytes = 4096u;
    constexpr uint64_t k_FarOffset = std::numeric_limits<uint64_t>::max() - 3u;
    const std::vector<uint32_t> patternWords = MakeWordPattern(k_BufferBytes / sizeof(uint32_t), 83u);
    const RhiBufferResourceData patternData{ std::as_bytes(std::span{ patternWords }), 0u };
    const GraphicsResource pattern = CreateBufferAndWait(DeviceLocalBuffer(k_BufferBytes), std::span{ &patternData, 1u });
    const GraphicsResource scratch = CreateReadbackBuffer(k_BufferBytes);
    rhi::OperationFuture fillReply = context->FillBuffer(scratch, k_SentinelWord, 0u, k_BufferBytes, {});
    ASSERT_EQ(context->Wait(fillReply, k_TimeoutNs), rhi::FutureState::Complete);
    const std::vector<uint32_t> sentinel = MakeSentinelWords(k_BufferBytes / sizeof(uint32_t));

    const auto expectRejected = [&](std::string_view name, rhi::OperationFuture future)
    {
        ExpectInvalidRegion(name, future);
        ExpectImageMatches(image, k_Resting, model);
        ExpectImageMatches(other, k_Resting, otherModel);
        EXPECT_EQ(ReadHostVisibleWords(*context, scratch, k_BufferBytes), sentinel);
    };

    const auto copyOut = [&](const BufferImageRegion& region)
    {
        return context->CopyImageToBufferRegions(image, k_Resting, scratch, std::span{ &region, 1u }, {});
    };

    const auto copyIn = [&](const BufferImageRegion& region)
    {
        return context->CopyBufferToImageRegions(pattern, image, k_Resting, std::span{ &region, 1u }, {});
    };

    const TexelBox whole0{ 0, 0, 16, 16 };
    const TexelBox whole1{ 0, 0, 8, 8 };

    // the image side of a region
    const auto outWithImage = [&](const ImageRegion& box)
    {
        return copyOut(MakeBufferImageRegion(0u, 0, 0, box));
    };
    expectRejected("width 0", outWithImage(MakeRegion(0, 0, 1, TexelBox{ 0, 0, 0, 4 })));
    expectRejected("height 0", outWithImage(MakeRegion(0, 0, 1, TexelBox{ 0, 0, 4, 0 })));
    expectRejected("layer count 0", outWithImage(MakeRegion(0, 0, 0, whole0)));
    ImageRegion negativeDepth = MakeRegion(0, 0, 1, whole0);
    negativeDepth.Depth = -1;
    expectRejected("negative depth", outWithImage(negativeDepth));
    ImageRegion deepBox = MakeRegion(0, 0, 1, whole0);
    deepBox.Depth = 2;
    expectRejected("depth 2 in a 2D image", outWithImage(deepBox));
    ImageRegion zOffset = MakeRegion(0, 0, 1, whole0);
    zOffset.Z = 1;
    expectRejected("z offset in a 2D image", outWithImage(zOffset));
    expectRejected("mip level past the last", outWithImage(MakeRegion(3, 0, 1, whole1)));
    expectRejected("negative mip level", outWithImage(MakeRegion(-1, 0, 1, whole1)));
    expectRejected("layers past the last", outWithImage(MakeRegion(0, 1, 2, whole0)));
    expectRejected("negative base layer", outWithImage(MakeRegion(0, -1, 1, whole0)));
    expectRejected("box past the edge of the mip level", outWithImage(MakeRegion(1, 0, 1, TexelBox{ 4, 0, 8, 4 })));
    expectRejected("negative x", outWithImage(MakeRegion(0, 0, 1, TexelBox{ -1, 0, 4, 4 })));
    expectRejected("box that overflows", outWithImage(MakeRegion(0, 0, 1, TexelBox{ std::numeric_limits<int32_t>::max(), 0, 8, 4 })));

    // the buffer side of a region
    const ImageRegion box0 = MakeRegion(0, 0, 1, whole0);
    expectRejected("buffer offset not a multiple of 4", copyOut(MakeBufferImageRegion(2u, 0, 0, box0)));
    expectRejected("row length below the width", copyOut(MakeBufferImageRegion(0u, 8, 0, box0)));
    expectRejected("negative row length", copyOut(MakeBufferImageRegion(0u, -1, 0, box0)));
    expectRejected("image height below the height", copyOut(MakeBufferImageRegion(0u, 0, 8, box0)));
    expectRejected("texels past the end of the buffer", copyOut(MakeBufferImageRegion(3500u, 0, 0, box0)));
    expectRejected("buffer offset that overflows", copyOut(MakeBufferImageRegion(k_FarOffset, 0, 0, box0)));
    expectRejected("row pitch that does not fit", copyOut(MakeBufferImageRegion(0u, std::numeric_limits<int32_t>::max(), 0, MakeRegion(0, 0, 2, whole0))));
    expectRejected("no regions", context->CopyImageToBufferRegions(image, k_Resting, scratch, std::span<const BufferImageRegion>{}, {}));
    const std::vector<BufferImageRegion> tooMany(static_cast<size_t>(k_MaxTransferRegions) + 1u, MakeBufferImageRegion(0u, 0, 0, MakeRegion(0, 0, 1, TexelBox{ 0, 0, 1, 1 })));
    expectRejected("257 regions", context->CopyImageToBufferRegions(image, k_Resting, scratch, tooMany, {}));

    // one good region and one bad region: the good one must not happen either
    const std::array<BufferImageRegion, 2> oneBadRegion
    {
        MakeBufferImageRegion(0u, 0, 0, MakeRegion(0, 0, 1, TexelBox{ 0, 0, 4, 4 })),
        MakeBufferImageRegion(1024u, 0, 0, MakeRegion(0, 0, 1, TexelBox{ 0, 0, 40, 4 })),
    };
    expectRejected("one bad region among good ones", context->CopyImageToBufferRegions(image, k_Resting, scratch, oneBadRegion, {}));

    // a buffer to image copy
    expectRejected("copy in: width 0", copyIn(MakeBufferImageRegion(0u, 0, 0, MakeRegion(0, 0, 1, TexelBox{ 0, 0, 0, 4 }))));
    expectRejected("copy in: box past the edge", copyIn(MakeBufferImageRegion(0u, 0, 0, MakeRegion(2, 0, 1, TexelBox{ 2, 2, 4, 4 }))));
    expectRejected("copy in: texels past the end of the buffer", copyIn(MakeBufferImageRegion(3500u, 0, 0, box0)));
    const std::array<BufferImageRegion, 2> overlappingBoxes
    {
        MakeBufferImageRegion(0u, 0, 0, MakeRegion(0, 0, 1, TexelBox{ 0, 0, 8, 8 })),
        MakeBufferImageRegion(1024u, 0, 0, MakeRegion(0, 0, 2, TexelBox{ 4, 4, 8, 8 })),
    };
    expectRejected("copy in: two boxes that overlap in the image", context->CopyBufferToImageRegions(pattern, image, k_Resting, overlappingBoxes, {}));

    // an image to image copy
    const ImageCopyRegion goodCopy{ MakeRegion(0, 0, 1, TexelBox{ 0, 0, 8, 8 }), MakeRegion(0, 1, 1, TexelBox{ 0, 0, 0, 0 }) };
    const ImageCopyRegion sourceOutside{ MakeRegion(0, 0, 1, TexelBox{ 12, 0, 8, 8 }), MakeRegion(0, 1, 1, TexelBox{ 0, 0, 0, 0 }) };
    expectRejected("image copy: source box past the edge", context->CopyImageRegions(image, k_Resting, other, k_Resting, std::span{ &sourceOutside, 1u }, {}));
    const ImageCopyRegion destinationOutside{ MakeRegion(0, 0, 1, TexelBox{ 0, 0, 8, 8 }), MakeRegion(0, 1, 1, TexelBox{ 12, 0, 0, 0 }) };
    expectRejected("image copy: destination box past the edge", context->CopyImageRegions(image, k_Resting, other, k_Resting, std::span{ &destinationOutside, 1u }, {}));
    const ImageCopyRegion destinationMip{ MakeRegion(0, 0, 1, TexelBox{ 0, 0, 8, 8 }), MakeRegion(3, 0, 1, TexelBox{ 0, 0, 0, 0 }) };
    expectRejected("image copy: destination mip level past the last", context->CopyImageRegions(image, k_Resting, other, k_Resting, std::span{ &destinationMip, 1u }, {}));
    const std::array<ImageCopyRegion, 2> overlappingDestinations
    {
        goodCopy,
        ImageCopyRegion{ MakeRegion(0, 0, 1, TexelBox{ 8, 8, 8, 8 }), MakeRegion(0, 1, 1, TexelBox{ 4, 4, 0, 0 }) },
    };
    expectRejected("image copy: two destination boxes that overlap", context->CopyImageRegions(image, k_Resting, other, k_Resting, overlappingDestinations, {}));
    expectRejected("image copy: no regions", context->CopyImageRegions(image, k_Resting, other, k_Resting, std::span<const ImageCopyRegion>{}, {}));

    // an upload
    const std::vector<uint32_t> sixteen = MakeWordPattern(16u, 84u);
    const auto upload = [&](const RhiImageResourceData& piece)
    {
        return context->SetImageData(image, k_Resting, std::span{ &piece, 1u }, {});
    };
    expectRejected("upload: width 0", upload(MakePiece(sixteen, 0, 0, 1, TexelBox{ 0, 0, 0, 4 })));
    expectRejected("upload: box past the edge", upload(MakePiece(sixteen, 0, 0, 1, TexelBox{ 14, 0, 4, 4 })));
    expectRejected("upload: mip level past the last", upload(MakePiece(sixteen, 3, 0, 1, TexelBox{ 0, 0, 4, 4 })));
    expectRejected("upload: layers past the last", upload(MakePiece(sixteen, 0, 1, 2, TexelBox{ 0, 0, 2, 2 })));
    expectRejected("upload: too few bytes", upload(MakePiece(sixteen, 0, 0, 1, TexelBox{ 0, 0, 5, 4 })));
    expectRejected("upload: too few bytes for two layers", upload(MakePiece(sixteen, 0, 0, 2, TexelBox{ 0, 0, 4, 4 })));
    const std::array<RhiImageResourceData, 2> overlappingPieces
    {
        MakePiece(sixteen, 0, 0, 1, TexelBox{ 0, 0, 4, 4 }),
        MakePiece(sixteen, 0, 0, 1, TexelBox{ 2, 2, 4, 4 }),
    };
    expectRejected("upload: two pieces that overlap", context->SetImageData(image, k_Resting, overlappingPieces, {}));
    expectRejected("upload: no pieces", context->SetImageData(image, k_Resting, std::span<const RhiImageResourceData>{}, {}));

    // a create call with a bad piece publishes no resource
    ImageCreateInfo info = RgbaImageInfo(model);
    const RhiImageResourceData createPiece = MakePiece(sixteen, 0, 0, 1, TexelBox{ 14, 14, 4, 4 });
    rhi::ResourceFuture<GraphicsResource> createReply = context->CreateImage(info, std::span{ &createPiece, 1u }, ImageUsageBits::SampledImage);
    EXPECT_EQ(context->Wait(createReply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(createReply.Error(), rhi::ResourceError::InvalidRegion);

    // two images with a different texel size have no copy
    ImageCreateInfo narrowInfo = RgbaImageInfo(model);
    narrowInfo.Format = ImageFormat{ ImageComponentFormats::R8, ImageDataType::UNorm };
    rhi::ResourceFuture<GraphicsResource> narrowReply = context->CreateImage(narrowInfo);
    ASSERT_EQ(context->Wait(narrowReply, k_TimeoutNs), rhi::FutureState::Complete);
    rhi::OperationFuture mismatchReply = context->CopyImageRegions(image, k_Resting, narrowReply.Get(), rhi::ImageUsageTransition{ ImageUsageBits::Discard, ImageUsageBits::SampledImage }, std::span{ &goodCopy, 1u }, {});
    EXPECT_EQ(context->Wait(mismatchReply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(mismatchReply.Error(), rhi::ResourceError::CopyShapeMismatch);
    DestroyAndWait(narrowReply.Get());
}

TEST_F(ImageRegionTest, ManyImageRegionsShareOneTicket)
{
    const ImageModel model = MakeImageModel(64, 4, 7, 91u);
    const GraphicsResource image = CreateFilledImage(model);

    // 256 one-texel regions of one image: mip 0, one row of the first layer for each region
    constexpr int32_t k_RegionCount = k_MaxTransferRegions;
    const GraphicsResource readback = CreateReadbackBuffer(static_cast<size_t>(k_RegionCount) * 4u);
    std::vector<BufferImageRegion> regions;
    std::vector<uint32_t> expected;
    for (int32_t index = 0; index < k_RegionCount; ++index)
    {
        const TexelBox box{ index % 64, index / 64, 1, 1 };
        regions.push_back(MakeBufferImageRegion(static_cast<uint64_t>(index) * 4u, 0, 0, MakeRegion(0, index % 4, 1, box)));
        expected.push_back(SubresourceOf(model, 0, index % 4)[static_cast<size_t>(box.Y * 64 + box.X)]);
    }

    rhi::OperationFuture probeReply = context->FillBuffer(readback, k_SentinelWord, 0u, static_cast<size_t>(k_RegionCount) * 4u, {});
    ASSERT_EQ(context->Wait(probeReply, k_TimeoutNs), rhi::FutureState::Complete);
    const uint64_t probeTicket = probeReply.Completion().Value;
    const int64_t recordedBefore = context->Counters().TransfersRecorded;

    rhi::OperationFuture copyReply = context->CopyImageToBufferRegions(image, k_Resting, readback, regions, {});
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(copyReply.Completion().Value, probeTicket + 1u);
    EXPECT_EQ(context->Counters().TransfersRecorded - recordedBefore, 1);
    EXPECT_EQ(CountMismatchedWords(ReadWordsAndDestroy(readback, static_cast<size_t>(k_RegionCount) * 4u), expected), 0);
}

TEST_F(ResourceContextTest, CreateBufferRefusesBadInitialPieces)
{
    constexpr uint64_t k_BufferBytes = 256u;
    const std::vector<uint32_t> words = MakeWordPattern(16u, 101u);
    const std::vector<uint32_t> otherWords = MakeWordPattern(16u, 102u);

    const RhiBufferResourceData pastTheEnd{ std::as_bytes(std::span{ words }), 224u };
    const std::array<RhiBufferResourceData, 2> overlapping
    {
        RhiBufferResourceData{ std::as_bytes(std::span{ words }), 0u },
        RhiBufferResourceData{ std::as_bytes(std::span{ otherWords }), 32u },
    };

    for (const BufferCreateInfo& info : { DeviceLocalBuffer(k_BufferBytes), HostVisibleBuffer(k_BufferBytes) })
    {
        const bool isHostVisible = info.Domain == ResourceDomain::CPUToGPU;
        SCOPED_TRACE(isHostVisible ? "host-visible buffer" : "device-local buffer");

        rhi::ResourceFuture<GraphicsResource> pastReply = context->CreateBuffer(info, std::span{ &pastTheEnd, 1u });
        EXPECT_EQ(context->Wait(pastReply, k_TimeoutNs), rhi::FutureState::Failed);
        EXPECT_EQ(pastReply.Error(), rhi::ResourceError::InvalidRegion);

        rhi::ResourceFuture<GraphicsResource> overlapReply = context->CreateBuffer(info, overlapping);
        EXPECT_EQ(context->Wait(overlapReply, k_TimeoutNs), rhi::FutureState::Failed);
        EXPECT_EQ(overlapReply.Error(), rhi::ResourceError::InvalidRegion);
    }
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

    ImageModel MakeImageModel(int32_t size, int32_t layer_count, int32_t mip_count, uint32_t seed)
    {
        ImageModel model;
        model.Size = size;
        model.LayerCount = layer_count;
        model.MipCount = mip_count;

        for (int32_t mipLevel = 0; mipLevel < mip_count; ++mipLevel)
        {
            const uint32_t edge = static_cast<uint32_t>(size >> mipLevel);
            for (int32_t layer = 0; layer < layer_count; ++layer)
            {
                // a different pattern for every subresource, so that a texel in the wrong one shows
                const uint32_t subresourceSeed = seed * 1000u + static_cast<uint32_t>(mipLevel) * 64u + static_cast<uint32_t>(layer);
                model.Subresources.push_back(MakeWordPattern(edge * edge, subresourceSeed));
            }
        }

        return model;
    }

    int32_t MipEdge(const ImageModel& model, int32_t mip_level)
    {
        return model.Size >> mip_level;
    }

    std::vector<uint32_t>& SubresourceOf(ImageModel& model, int32_t mip_level, int32_t layer)
    {
        return model.Subresources[static_cast<size_t>(mip_level * model.LayerCount + layer)];
    }

    const std::vector<uint32_t>& SubresourceOf(const ImageModel& model, int32_t mip_level, int32_t layer)
    {
        return model.Subresources[static_cast<size_t>(mip_level * model.LayerCount + layer)];
    }

    std::vector<uint32_t> CropBox(const ImageModel& model, int32_t mip_level, int32_t layer, const TexelBox& box)
    {
        const std::vector<uint32_t>& texels = SubresourceOf(model, mip_level, layer);
        const int32_t edge = MipEdge(model, mip_level);

        std::vector<uint32_t> crop;
        for (int32_t row = 0; row < box.Height; ++row)
        {
            for (int32_t column = 0; column < box.Width; ++column)
            {
                crop.push_back(texels[static_cast<size_t>((box.Y + row) * edge + box.X + column)]);
            }
        }

        return crop;
    }

    void WriteBox(ImageModel& model, int32_t mip_level, int32_t layer, const TexelBox& box, std::span<const uint32_t> texels)
    {
        std::vector<uint32_t>& target = SubresourceOf(model, mip_level, layer);
        const int32_t edge = MipEdge(model, mip_level);

        for (int32_t row = 0; row < box.Height; ++row)
        {
            for (int32_t column = 0; column < box.Width; ++column)
            {
                target[static_cast<size_t>((box.Y + row) * edge + box.X + column)] = texels[static_cast<size_t>(row * box.Width + column)];
            }
        }
    }

    ImageCreateInfo RgbaImageInfo(const ImageModel& model)
    {
        ImageCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = ImageUsageBits::SampledImage | ImageUsageBits::ComputeShaderSRV | ImageUsageBits::TransferSource | ImageUsageBits::TransferDestination;
        info.Type = ImageType::Type2D;
        info.Format = ImageFormat{ ImageComponentFormats::RGBA8, ImageDataType::UNorm };
        info.Width = static_cast<uint32_t>(model.Size);
        info.Height = static_cast<uint32_t>(model.Size);
        info.Depth = 1u;
        info.MipLevels = static_cast<uint32_t>(model.MipCount);
        info.ArrayLayers = static_cast<uint32_t>(model.LayerCount);
        return info;
    }

    ImageRegion MakeRegion(int32_t mip_level, int32_t base_layer, int32_t layer_count, const TexelBox& box)
    {
        return ImageRegion{ mip_level, base_layer, layer_count, box.X, box.Y, 0, box.Width, box.Height, 1 };
    }

    BufferImageRegion MakeBufferImageRegion(uint64_t buffer_offset, int32_t row_texels, int32_t image_rows, const ImageRegion& image)
    {
        return BufferImageRegion{ buffer_offset, row_texels, image_rows, image };
    }

    std::vector<RhiImageResourceData> MakeFullUpload(const ImageModel& model)
    {
        std::vector<RhiImageResourceData> pieces;
        for (int32_t mipLevel = 0; mipLevel < model.MipCount; ++mipLevel)
        {
            const int32_t edge = MipEdge(model, mipLevel);
            for (int32_t layer = 0; layer < model.LayerCount; ++layer)
            {
                pieces.push_back(MakePiece(SubresourceOf(model, mipLevel, layer), mipLevel, layer, 1, TexelBox{ 0, 0, edge, edge }));
            }
        }

        return pieces;
    }

    std::vector<BufferImageRegion> MakeWholeReadbackRegions(const ImageModel& model)
    {
        std::vector<BufferImageRegion> regions;
        uint64_t offset = 0u;

        for (int32_t mipLevel = 0; mipLevel < model.MipCount; ++mipLevel)
        {
            const int32_t edge = MipEdge(model, mipLevel);
            for (int32_t layer = 0; layer < model.LayerCount; ++layer)
            {
                regions.push_back(MakeBufferImageRegion(offset, 0, 0, MakeRegion(mipLevel, layer, 1, TexelBox{ 0, 0, edge, edge })));
                offset += static_cast<uint64_t>(edge) * static_cast<uint64_t>(edge) * sizeof(uint32_t);
            }
        }

        return regions;
    }

    uint64_t WholeReadbackBytes(const ImageModel& model)
    {
        uint64_t bytes = 0u;
        for (const std::vector<uint32_t>& subresource : model.Subresources)
        {
            bytes += subresource.size() * sizeof(uint32_t);
        }

        return bytes;
    }

    RhiImageResourceData MakePiece(std::span<const uint32_t> texels, int32_t mip_level, int32_t layer, int32_t layer_count, const TexelBox& box)
    {
        RhiImageResourceData piece{};
        piece.Data = std::as_bytes(texels);
        piece.Width = box.Width;
        piece.Height = box.Height;
        piece.X = box.X;
        piece.Y = box.Y;
        piece.MipLevel = mip_level;
        piece.ArrayLayer = layer;
        piece.NumLayers = layer_count;
        return piece;
    }

    int32_t CountMismatchedWords(std::span<const uint32_t> actual, std::span<const uint32_t> expected)
    {
        if (actual.size() != expected.size())
        {
            ADD_FAILURE() << "the sizes differ: " << actual.size() << " and " << expected.size();
            return -1;
        }

        int32_t mismatches = 0;
        for (size_t index = 0u; index < actual.size(); ++index)
        {
            if (actual[index] != expected[index])
            {
                ++mismatches;
            }
        }

        return mismatches;
    }

    void PlaceBoxInBuffer(std::vector<uint32_t>& words, uint64_t buffer_offset, int32_t row_texels, int32_t image_rows, int32_t layer_slot, int32_t box_width, int32_t box_height, std::span<const uint32_t> texels)
    {
        const int32_t rowLength = row_texels != 0 ? row_texels : box_width;
        const int32_t imageHeight = image_rows != 0 ? image_rows : box_height;
        const size_t firstWord = static_cast<size_t>(buffer_offset / sizeof(uint32_t)) + static_cast<size_t>(layer_slot) * static_cast<size_t>(imageHeight) * static_cast<size_t>(rowLength);

        for (int32_t row = 0; row < box_height; ++row)
        {
            for (int32_t column = 0; column < box_width; ++column)
            {
                words[firstWord + static_cast<size_t>(row * rowLength + column)] = texels[static_cast<size_t>(row * box_width + column)];
            }
        }
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

GraphicsResource ImageRegionTest::CreateFilledImage(const ImageModel& model)
{
    const std::vector<RhiImageResourceData> pieces = MakeFullUpload(model);
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateImage(RgbaImageInfo(model), pieces, ImageUsageBits::SampledImage);
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    return reply.Get();
}

void ImageRegionTest::ExpectImageMatches(const GraphicsResource& image, rhi::ImageUsageTransition usage, const ImageModel& model)
{
    const std::vector<BufferImageRegion> regions = MakeWholeReadbackRegions(model);
    const uint64_t bytes = WholeReadbackBytes(model);

    const GraphicsResource readback = CreateReadbackBuffer(bytes);
    rhi::OperationFuture reply = context->CopyImageToBufferRegions(image, usage, readback, regions, {});
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    const std::vector<uint32_t> words = ReadWordsAndDestroy(readback, bytes);

    size_t firstWord = 0u;
    for (int32_t mipLevel = 0; mipLevel < model.MipCount; ++mipLevel)
    {
        for (int32_t layer = 0; layer < model.LayerCount; ++layer)
        {
            const std::vector<uint32_t>& expected = SubresourceOf(model, mipLevel, layer);
            SCOPED_TRACE("mip level " + std::to_string(mipLevel) + ", layer " + std::to_string(layer));
            EXPECT_EQ(CountMismatchedWords(std::span<const uint32_t>(words).subspan(firstWord, expected.size()), expected), 0);
            firstWord += expected.size();
        }
    }
}

void ImageRegionTest::ExpectInvalidRegion(std::string_view name, const rhi::OperationFuture& future)
{
    SCOPED_TRACE(std::string{ name });
    EXPECT_EQ(context->Wait(future, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(future.Error(), rhi::ResourceError::InvalidRegion);
}
