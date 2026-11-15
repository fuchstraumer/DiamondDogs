#include "ResourceContextFixture.hpp"
#include "Device.hpp"
#include "RhiSystemStart.hpp"
#include "StartTestRhi.hpp"
#include <cstring>
#include <format>
#include <iostream>

std::unique_ptr<rhi::RhiSystem> ResourceContextTest::rhiSystem = nullptr;

void ResourceContextTest::SetUpTestSuite()
{
    const test_rhi::Needs needs
    {
        .DeviceExtensions = test_rhi::k_MemoryBudgetExtension,
    };

    rhiSystem = std::make_unique<rhi::RhiSystem>();

    const rhi::RhiSystemResult started = test_rhi::StartTestRhi(*rhiSystem, "ResourceContextTests", rhi::ApiVersion::Latest, rhi::ValidationLayers::Full, needs);

    // SetUp asserts on a null system, so every test reports the failure
    if (!started.Succeeded())
    {
        std::cerr << std::format("ResourceContextTests: RhiSystem did not start: {}: {}\n", started.Subject, started.Detail);
        rhiSystem.reset();
    }
}

void ResourceContextTest::TearDownTestSuite()
{
    rhiSystem.reset();
}

void ResourceContextTest::SetUp()
{
    ASSERT_NE(rhiSystem, nullptr);
    RecreateContext(k_DefaultMaxFutures);
}

void ResourceContextTest::TearDown()
{
    // destroys every resource the test left alive, which is part of what the suite checks
    context.reset();
}

void ResourceContextTest::RecreateContext(int32_t max_futures)
{
    RecreateContextWithRing(max_futures, k_DefaultStagingRingSize);
}

void ResourceContextTest::RecreateContextWithRing(int32_t max_futures, uint64_t staging_ring_size)
{
    context.reset();
    context = std::make_unique<ResourceContext>();
    const ResourceContextCreateInfo createInfo
    {
        rhiSystem->GetInstance(),
        rhiSystem->GetDevice(),
        rhiSystem->GetVulkanApiVersion(),
        true,
        false,
        max_futures,
        staging_ring_size
    };
    ASSERT_EQ(context->Initialize(createInfo), ResourceContextError::Success);
}

std::vector<uint32_t> ResourceContextTest::ReadBackWords(const GraphicsResource& source, size_t size_bytes)
{
    GraphicsResource readback = CreateReadbackBuffer(size_bytes);

    rhi::OperationFuture copyReply = context->CopyBufferContents(source, readback, {});
    EXPECT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    return ReadWordsAndDestroy(readback, size_bytes);
}

std::vector<uint32_t> ResourceContextTest::ReadBackImageWords(const GraphicsResource& image, rhi::ImageUsageTransition usage, size_t size_bytes)
{
    GraphicsResource readback = CreateReadbackBuffer(size_bytes);

    rhi::OperationFuture copyReply = context->CopyImageToBuffer(image, usage, readback, {});
    EXPECT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

    return ReadWordsAndDestroy(readback, size_bytes);
}

GraphicsResource ResourceContextTest::CreateReadbackBuffer(size_t size_bytes)
{
    BufferCreateInfo readbackInfo{};
    readbackInfo.Domain = ResourceDomain::GPUToCPU;
    readbackInfo.Usage = BufferUsageBits::CopyDestination;
    readbackInfo.Size = size_bytes;
    return CreateBufferAndWait(readbackInfo, {});
}

std::vector<uint32_t> ResourceContextTest::ReadWordsAndDestroy(const GraphicsResource& readback, size_t size_bytes)
{
    rhi::ResourceFuture<void*> mapReply = context->MapBuffer(readback, size_bytes, 0u);
    EXPECT_EQ(context->Wait(mapReply, k_TimeoutNs), rhi::FutureState::Complete);
    std::vector<uint32_t> words(size_bytes / sizeof(uint32_t));
    if (mapReply.Get() != nullptr)
    {
        std::memcpy(words.data(), mapReply.Get(), size_bytes);
    }

    rhi::OperationFuture unmapReply = context->UnmapBuffer(readback, size_bytes, 0u);
    EXPECT_EQ(context->Wait(unmapReply, k_TimeoutNs), rhi::FutureState::Complete);
    DestroyAndWait(readback);

    return words;
}

GraphicsResource ResourceContextTest::CreateBufferAndWait(const BufferCreateInfo& create_info, std::span<const RhiBufferResourceData> initial_data)
{
    rhi::ResourceFuture<GraphicsResource> reply = context->CreateBuffer(create_info, initial_data);
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
    return reply.Get();
}

void ResourceContextTest::DestroyAndWait(const GraphicsResource& resource)
{
    rhi::OperationFuture reply = context->DestroyResource(resource);
    EXPECT_EQ(context->Wait(reply, k_TimeoutNs), rhi::FutureState::Complete);
}

std::vector<uint32_t> MakeWordPattern(uint32_t count, uint32_t seed)
{
    std::vector<uint32_t> words(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        words[i] = (i * 2654435761u) ^ seed;
    }
    return words;
}
