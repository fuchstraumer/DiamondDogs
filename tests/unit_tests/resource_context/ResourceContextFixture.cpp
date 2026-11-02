#include "ResourceContextFixture.hpp"
#include "Device.hpp"
#include <cstring>

std::unique_ptr<rhi::RhiSystem> ResourceContextTest::rhiSystem = nullptr;

void ResourceContextTest::SetUpTestSuite()
{
    rhi::RhiSystemCreateInfo createInfo{};
    createInfo.ApplicationName = "ResourceContextTests";
    createInfo.VkVersion = rhi::ApiVersion::Latest;
    createInfo.ValidationLevel = rhi::ValidationLayers::Full;
    createInfo.RequiredInstanceExtensions = { "VK_EXT_debug_utils" };
    rhiSystem = std::make_unique<rhi::RhiSystem>(createInfo);
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
    context.reset();
    context = std::make_unique<ResourceContext>();
    const ResourceContextCreateInfo createInfo
    {
        rhiSystem->GetInstance(),
        rhiSystem->GetDevice(),
        rhiSystem->GetVulkanApiVersion(),
        true,
        false,
        max_futures
    };
    ASSERT_EQ(context->Initialize(createInfo), ResourceContextError::Success);
}

std::vector<uint32_t> ResourceContextTest::ReadBackWords(const GraphicsResource& source, size_t size_bytes)
{
    BufferCreateInfo readbackInfo{};
    readbackInfo.Domain = ResourceDomain::GPUToCPU;
    readbackInfo.Usage = BufferUsageBits::CopyDestination;
    readbackInfo.Size = size_bytes;
    GraphicsResource readback = CreateBufferAndWait(readbackInfo, {});

    rhi::OperationFuture copyReply = context->CopyBufferContents(source, readback);
    EXPECT_EQ(context->Wait(copyReply, k_TimeoutNs), rhi::FutureState::Complete);

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
