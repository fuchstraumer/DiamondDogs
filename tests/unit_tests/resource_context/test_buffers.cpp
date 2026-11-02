#include "ImageDataFormats.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFlags.hpp"
#include "ResourceMessageReply.hpp"
#include "ResourceTypes.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
    BufferCreateInfo DeviceLocalStorageBuffer(size_t size)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        // transfer usage is opt-in. the tests fill these buffers and read them back, so they ask for both bits
        info.Usage = BufferUsageBits::ComputeShaderUAV | BufferUsageBits::CopySource | BufferUsageBits::CopyDestination;
        info.Size = size;
        return info;
    }
}

TEST_F(ResourceContextTest, HostVisibleBufferIsWrittenDirectly)
{
    const std::vector<uint32_t> words = MakeWordPattern(64u, 0x1234u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    BufferCreateInfo info{};
    info.Domain = ResourceDomain::CPUToGPU;
    info.Usage = BufferUsageBits::ComputeShaderUBO;
    info.Size = data.Data.size();

    std::shared_ptr<GraphicsResourceReply> reply = context->CreateBuffer(info, std::span{ &data, 1u });
    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    const GraphicsResource buffer = reply->GetResource();
    ASSERT_TRUE(static_cast<bool>(buffer));
    EXPECT_EQ(buffer.Type, ResourceType::Buffer);
    EXPECT_EQ(buffer.Domain, ResourceDomain::CPUToGPU);

    std::shared_ptr<PointerMessageReply> mapReply = context->MapBuffer(buffer, data.Data.size(), 0u);
    ASSERT_EQ(mapReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    ASSERT_NE(mapReply->GetPointer(), nullptr);
    EXPECT_EQ(std::memcmp(mapReply->GetPointer(), words.data(), data.Data.size()), 0);
    std::shared_ptr<MessageReply> unmapReply = context->UnmapBuffer(buffer, data.Data.size(), 0u);
    EXPECT_EQ(unmapReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);

    DestroyAndWait(buffer);
}

TEST_F(ResourceContextTest, DeviceLocalBufferUploadsThroughTransferSystem)
{
    const std::vector<uint32_t> words = MakeWordPattern(4096u, 0xBEEFu);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

    std::shared_ptr<GraphicsResourceReply> reply = context->CreateBuffer(DeviceLocalStorageBuffer(data.Data.size()), std::span{ &data, 1u });
    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    // the transfer signals the transfer timeline at the reply's ticket, which other queues wait on before they use the buffer
    EXPECT_NE(reply->SemaphoreHandle(), 0u);
    EXPECT_GT(reply->SemaphoreValue(), 0u);
    EXPECT_TRUE(reply->IsSubmitted());

    const GraphicsResource buffer = reply->GetResource();
    EXPECT_EQ(ReadBackWords(buffer, data.Data.size()), words);
}

TEST_F(ResourceContextTest, InitialDataRegionsAreWrittenBackToBack)
{
    const std::vector<uint32_t> first = MakeWordPattern(100u, 1u);
    const std::vector<uint32_t> second = MakeWordPattern(28u, 2u);
    const std::array<RhiBufferResourceData, 2> data
    {
        RhiBufferResourceData{ std::as_bytes(std::span{ first }), 0u },
        RhiBufferResourceData{ std::as_bytes(std::span{ second }), 0u },
    };
    const size_t totalBytes = data[0].Data.size() + data[1].Data.size();

    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(totalBytes), data);

    std::vector<uint32_t> expected = first;
    expected.insert(expected.end(), second.begin(), second.end());
    EXPECT_EQ(ReadBackWords(buffer, totalBytes), expected);
}

TEST_F(ResourceContextTest, SetBufferDataReplacesContents)
{
    const std::vector<uint32_t> words = MakeWordPattern(256u, 7u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(data.Data.size()), {});

    std::shared_ptr<ResourceTransferReply> setReply = context->SetBufferData(buffer, std::span{ &data, 1u });
    ASSERT_EQ(setReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    EXPECT_EQ(ReadBackWords(buffer, data.Data.size()), words);
}

TEST_F(ResourceContextTest, FillBufferWritesTheValue)
{
    constexpr size_t k_Bytes = 1024u;
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(k_Bytes), {});

    std::shared_ptr<ResourceTransferReply> fillReply = context->FillBuffer(buffer, 0xA5A5A5A5u, 0u, k_Bytes);
    ASSERT_EQ(fillReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    EXPECT_EQ(ReadBackWords(buffer, k_Bytes), std::vector<uint32_t>(k_Bytes / sizeof(uint32_t), 0xA5A5A5A5u));
}

TEST_F(ResourceContextTest, FillBufferRejectsAnUnalignedOffset)
{
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(64u), {});
    std::shared_ptr<ResourceTransferReply> fillReply = context->FillBuffer(buffer, 0u, 2u, 16u);
    EXPECT_EQ(fillReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
}

TEST_F(ResourceContextTest, CopyBufferDuplicatesContents)
{
    const std::vector<uint32_t> words = MakeWordPattern(512u, 99u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalStorageBuffer(data.Data.size()), std::span{ &data, 1u });

    std::shared_ptr<GraphicsResourceReply> copyReply = context->CopyBuffer(source, true);
    ASSERT_EQ(copyReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    const GraphicsResource copy = copyReply->GetResource();
    ASSERT_TRUE(static_cast<bool>(copy));
    EXPECT_NE(copy.VkHandle, source.VkHandle);
    EXPECT_EQ(ReadBackWords(copy, data.Data.size()), words);
}

TEST_F(ResourceContextTest, TexelBufferViewIsCreatedWhenFormatIsGiven)
{
    BufferCreateInfo info = DeviceLocalStorageBuffer(256u);
    info.ViewFormat = ImageFormat{ ImageComponentFormats::R32, ImageDataType::Float };
    const GraphicsResource buffer = CreateBufferAndWait(info, {});
    EXPECT_NE(buffer.VkViewHandle, 0u);
}

TEST_F(ResourceContextTest, NamedBufferKeepsItsName)
{
    BufferCreateInfo info = DeviceLocalStorageBuffer(64u);
    info.Name = "NamedBufferKeepsItsName";
    const GraphicsResource buffer = CreateBufferAndWait(info, {});
    EXPECT_TRUE(static_cast<bool>(buffer));
}

TEST_F(ResourceContextTest, DeviceAddressUsageIsRefusedWhenNotEnabled)
{
    BufferCreateInfo info = DeviceLocalStorageBuffer(64u);
    info.Usage = BufferUsageBits::DeviceAddressBuffer;
    std::shared_ptr<GraphicsResourceReply> reply = context->CreateBuffer(info, {});
    EXPECT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
}

TEST_F(ResourceContextTest, ZeroSizeBufferFails)
{
    std::shared_ptr<GraphicsResourceReply> reply = context->CreateBuffer(DeviceLocalStorageBuffer(0u), {});
    EXPECT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
    EXPECT_FALSE(static_cast<bool>(reply->GetResource()));
}

TEST_F(ResourceContextTest, DestroyingTwiceFailsTheSecondTime)
{
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(64u), {});
    DestroyAndWait(buffer);
    std::shared_ptr<MessageReply> second = context->DestroyResource(buffer);
    EXPECT_EQ(second->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
}

TEST_F(ResourceContextTest, MapRefusesAStaleHandle)
{
    BufferCreateInfo info{};
    info.Domain = ResourceDomain::CPUToGPU;
    info.Usage = BufferUsageBits::ComputeShaderUBO;
    info.Size = 64u;
    const GraphicsResource buffer = CreateBufferAndWait(info, {});
    DestroyAndWait(buffer);
    std::shared_ptr<PointerMessageReply> mapReply = context->MapBuffer(buffer, 64u, 0u);
    EXPECT_EQ(mapReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
    EXPECT_EQ(mapReply->GetPointer(), nullptr);
}

TEST_F(ResourceContextTest, DestroyUnmapsABufferTheUserLeftMapped)
{
    // VMA asserts when a mapped allocation is destroyed, so the context undoes the maps that MapBuffer made
    BufferCreateInfo info{};
    info.Domain = ResourceDomain::GPUToCPU;
    info.Usage = BufferUsageBits::CopyDestination;
    info.Size = 256u;
    const GraphicsResource buffer = CreateBufferAndWait(info, {});
    std::shared_ptr<PointerMessageReply> mapReply = context->MapBuffer(buffer, info.Size, 0u);
    ASSERT_EQ(mapReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    DestroyAndWait(buffer);
}

TEST_F(ResourceContextTest, TransferWithoutTheUsageBitFails)
{
    BufferCreateInfo info{};
    info.Domain = ResourceDomain::GPUOnly;
    info.Usage = BufferUsageBits::ComputeShaderUAV;
    info.Size = 64u;
    const GraphicsResource buffer = CreateBufferAndWait(info, {});

    std::shared_ptr<ResourceTransferReply> fillReply = context->FillBuffer(buffer, 0u, 0u, 64u);
    EXPECT_EQ(fillReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);

    BufferCreateInfo readbackInfo{};
    readbackInfo.Domain = ResourceDomain::GPUToCPU;
    readbackInfo.Usage = BufferUsageBits::CopyDestination;
    readbackInfo.Size = 64u;
    const GraphicsResource readback = CreateBufferAndWait(readbackInfo, {});
    std::shared_ptr<ResourceTransferReply> copyReply = context->CopyBufferContents(buffer, readback);
    EXPECT_EQ(copyReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
}

TEST_F(ResourceContextTest, StagedInitialDataAddsOnlyTheDestinationBit)
{
    // the upload needs CopyDestination, so the context adds it. the caller did not ask for CopySource, so a copy out fails
    const std::vector<uint32_t> words = MakeWordPattern(16u, 3u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    BufferCreateInfo info{};
    info.Domain = ResourceDomain::GPUOnly;
    info.Usage = BufferUsageBits::ComputeShaderSRV;
    info.Size = data.Data.size();
    const GraphicsResource buffer = CreateBufferAndWait(info, std::span{ &data, 1u });

    std::shared_ptr<GraphicsResourceReply> copyReply = context->CopyBuffer(buffer, true);
    EXPECT_EQ(copyReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);
}

TEST_F(ResourceContextTest, EveryBufferMessageRefusesAMismatchedHandle)
{
    // the entity is alive, but the buffer handle is not the one the registry stores, as for a stale GraphicsResource
    // whose entity index and version were reused. every message must refuse it, not only Map and Destroy
    constexpr size_t k_Bytes = 64u;
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(k_Bytes), {});
    GraphicsResource forged = buffer;
    forged.VkHandle = buffer.VkHandle ^ 0x10u;

    const std::vector<uint32_t> words = MakeWordPattern(16u, 7u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    std::shared_ptr<ResourceTransferReply> setReply = context->SetBufferData(forged, std::span{ &data, 1u });
    EXPECT_EQ(setReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);

    std::shared_ptr<ResourceTransferReply> fillReply = context->FillBuffer(forged, 0u, 0u, k_Bytes);
    EXPECT_EQ(fillReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);

    std::shared_ptr<ResourceTransferReply> copyContentsReply = context->CopyBufferContents(forged, buffer);
    EXPECT_EQ(copyContentsReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);

    std::shared_ptr<GraphicsResourceReply> copyReply = context->CopyBuffer(forged, true);
    EXPECT_EQ(copyReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);

    std::shared_ptr<MessageReply> destroyReply = context->DestroyResource(forged);
    EXPECT_EQ(destroyReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Failed);

    // the real handle still works, so the refusals above did not touch the buffer
    DestroyAndWait(buffer);
}

TEST_F(ResourceContextTest, NamesAreCopiedWhenTheCallIsMade)
{
    // the name outlives nothing: its string is freed and overwritten before the worker can read the message. names
    // longer than the copy's capacity are truncated, and the view takes a suffix that no longer fits
    std::shared_ptr<GraphicsResourceReply> reply;
    {
        std::string name(200u, 'n');
        BufferCreateInfo info = DeviceLocalStorageBuffer(256u);
        info.ViewFormat = ImageFormat{ ImageComponentFormats::R32, ImageDataType::Float };
        info.Name = name;
        reply = context->CreateBuffer(info, {});
        name.assign(name.size(), 'x');
    }

    ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    DestroyAndWait(reply->GetResource());
}
