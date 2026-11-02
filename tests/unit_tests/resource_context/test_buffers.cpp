#include "ImageDataFormats.hpp"
#include "ResourceContext.hpp"
#include "ResourceContextFixture.hpp"
#include "ResourceFlags.hpp"
#include "ResourceFuture.hpp"
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

    rhi::ResourceFuture<GraphicsResource> reply = context->CreateBuffer(info, std::span{ &data, 1u });
    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource buffer = reply.Get();
    ASSERT_TRUE(static_cast<bool>(buffer));
    EXPECT_EQ(buffer.Type, ResourceType::Buffer);
    EXPECT_EQ(buffer.Domain, ResourceDomain::CPUToGPU);

    rhi::ResourceFuture<void*> mapReply = context->MapBuffer(buffer, data.Data.size(), 0u);
    ASSERT_EQ(context->Wait(mapReply, k_TimeoutNs), rhi::FutureState::Complete);
    ASSERT_NE(mapReply.Get(), nullptr);
    EXPECT_EQ(std::memcmp(mapReply.Get(), words.data(), data.Data.size()), 0);
    rhi::OperationFuture unmapReply = context->UnmapBuffer(buffer, data.Data.size(), 0u);
    EXPECT_EQ(context->Wait(unmapReply, k_TimeoutNs), rhi::FutureState::Complete);

    DestroyAndWait(buffer);
}

TEST_F(ResourceContextTest, DeviceLocalBufferUploadsThroughTransferSystem)
{
    const std::vector<uint32_t> words = MakeWordPattern(4096u, 0xBEEFu);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };

    rhi::ResourceFuture<GraphicsResource> reply = context->CreateBuffer(DeviceLocalStorageBuffer(data.Data.size()), std::span{ &data, 1u });
    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    // the transfer signals the transfer timeline at the reply's ticket, which other queues wait on before they use the buffer
    EXPECT_NE(reply.Completion().Semaphore.Get(), 0u);
    EXPECT_GT(reply.Completion().Value, 0u);
    EXPECT_TRUE(reply.Completion().Submitted);

    const GraphicsResource buffer = reply.Get();
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

    rhi::OperationFuture setReply = context->SetBufferData(buffer, std::span{ &data, 1u });
    ASSERT_EQ(context->Wait(setReply, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackWords(buffer, data.Data.size()), words);
}

TEST_F(ResourceContextTest, FillBufferWritesTheValue)
{
    constexpr size_t k_Bytes = 1024u;
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(k_Bytes), {});

    rhi::OperationFuture fillReply = context->FillBuffer(buffer, 0xA5A5A5A5u, 0u, k_Bytes);
    ASSERT_EQ(context->Wait(fillReply, k_TimeoutNs), rhi::FutureState::Complete);
    EXPECT_EQ(ReadBackWords(buffer, k_Bytes), std::vector<uint32_t>(k_Bytes / sizeof(uint32_t), 0xA5A5A5A5u));
}

TEST_F(ResourceContextTest, FillBufferRejectsAnUnalignedOffset)
{
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(64u), {});
    rhi::OperationFuture fillReply = context->FillBuffer(buffer, 0u, 2u, 16u);
    EXPECT_EQ(context->Wait(fillReply, k_TimeoutNs), rhi::FutureState::Failed);
}

TEST_F(ResourceContextTest, CopyBufferDuplicatesContents)
{
    const std::vector<uint32_t> words = MakeWordPattern(512u, 99u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    const GraphicsResource source = CreateBufferAndWait(DeviceLocalStorageBuffer(data.Data.size()), std::span{ &data, 1u });

    rhi::ResourceFuture<GraphicsResource> copyReply = context->CopyBuffer(source, true);
    ASSERT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);
    const GraphicsResource copy = copyReply.Get();
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
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateBuffer(info, {});
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Failed);
}

TEST_F(ResourceContextTest, ZeroSizeBufferFails)
{
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateBuffer(DeviceLocalStorageBuffer(0u), {});
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_FALSE(static_cast<bool>(reply.Get()));
}

TEST_F(ResourceContextTest, DestroyingTwiceFailsTheSecondTime)
{
    const GraphicsResource buffer = CreateBufferAndWait(DeviceLocalStorageBuffer(64u), {});
    DestroyAndWait(buffer);
    rhi::OperationFuture second = context->DestroyResource(buffer);
    EXPECT_EQ(context->Wait(second, k_TimeoutNs), rhi::FutureState::Failed);
}

TEST_F(ResourceContextTest, MapRefusesAStaleHandle)
{
    BufferCreateInfo info{};
    info.Domain = ResourceDomain::CPUToGPU;
    info.Usage = BufferUsageBits::ComputeShaderUBO;
    info.Size = 64u;
    const GraphicsResource buffer = CreateBufferAndWait(info, {});
    DestroyAndWait(buffer);
    rhi::ResourceFuture<void*> mapReply = context->MapBuffer(buffer, 64u, 0u);
    EXPECT_EQ(context->Wait(mapReply, k_TimeoutNs), rhi::FutureState::Failed);
    EXPECT_EQ(mapReply.Get(), nullptr);
}

TEST_F(ResourceContextTest, DestroyUnmapsABufferTheUserLeftMapped)
{
    // VMA asserts when a mapped allocation is destroyed, so the context undoes the maps that MapBuffer made
    BufferCreateInfo info{};
    info.Domain = ResourceDomain::GPUToCPU;
    info.Usage = BufferUsageBits::CopyDestination;
    info.Size = 256u;
    const GraphicsResource buffer = CreateBufferAndWait(info, {});
    rhi::ResourceFuture<void*> mapReply = context->MapBuffer(buffer, info.Size, 0u);
    ASSERT_EQ(context->Wait(mapReply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(buffer);
}

TEST_F(ResourceContextTest, TransferWithoutTheUsageBitFails)
{
    BufferCreateInfo info{};
    info.Domain = ResourceDomain::GPUOnly;
    info.Usage = BufferUsageBits::ComputeShaderUAV;
    info.Size = 64u;
    const GraphicsResource buffer = CreateBufferAndWait(info, {});

    rhi::OperationFuture fillReply = context->FillBuffer(buffer, 0u, 0u, 64u);
    EXPECT_EQ(context->Wait(fillReply, k_TimeoutNs), rhi::FutureState::Failed);

    BufferCreateInfo readbackInfo{};
    readbackInfo.Domain = ResourceDomain::GPUToCPU;
    readbackInfo.Usage = BufferUsageBits::CopyDestination;
    readbackInfo.Size = 64u;
    const GraphicsResource readback = CreateBufferAndWait(readbackInfo, {});
    rhi::OperationFuture copyReply = context->CopyBufferContents(buffer, readback);
    EXPECT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Failed);
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

    rhi::ResourceFuture<GraphicsResource> copyReply = context->CopyBuffer(buffer, true);
    EXPECT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Failed);
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
    rhi::OperationFuture setReply = context->SetBufferData(forged, std::span{ &data, 1u });
    EXPECT_EQ(context->Wait(setReply, k_TimeoutNs), rhi::FutureState::Failed);

    rhi::OperationFuture fillReply = context->FillBuffer(forged, 0u, 0u, k_Bytes);
    EXPECT_EQ(context->Wait(fillReply, k_TimeoutNs), rhi::FutureState::Failed);

    rhi::OperationFuture copyContentsReply = context->CopyBufferContents(forged, buffer);
    EXPECT_EQ(context->Wait(copyContentsReply, k_TimeoutNs), rhi::FutureState::Failed);

    rhi::ResourceFuture<GraphicsResource> copyReply = context->CopyBuffer(forged, true);
    EXPECT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Failed);

    rhi::OperationFuture destroyReply = context->DestroyResource(forged);
    EXPECT_EQ(context->Wait(destroyReply, k_TimeoutNs), rhi::FutureState::Failed);

    // the real handle still works, so the refusals above did not touch the buffer
    DestroyAndWait(buffer);
}

TEST_F(ResourceContextTest, NamesAreCopiedWhenTheCallIsMade)
{
    // the name outlives nothing: its string is freed and overwritten before the worker can read the message. names
    // longer than the copy's capacity are truncated, and the view takes a suffix that no longer fits
    rhi::ResourceFuture<GraphicsResource> reply;
    {
        std::string name(200u, 'n');
        BufferCreateInfo info = DeviceLocalStorageBuffer(256u);
        info.ViewFormat = ImageFormat{ ImageComponentFormats::R32, ImageDataType::Float };
        info.Name = name;
        reply = context->CreateBuffer(info, {});
        name.assign(name.size(), 'x');
    }

    ASSERT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(reply.Get());
}
