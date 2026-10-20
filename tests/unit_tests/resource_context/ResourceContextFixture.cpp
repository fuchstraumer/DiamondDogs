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
    context = std::make_unique<ResourceContext>();
    const ResourceContextCreateInfo createInfo
    {
        rhiSystem->GetInstance(),
        rhiSystem->GetDevice(),
        rhiSystem->GetVulkanApiVersion(),
        true
    };
    context->Initialize(createInfo);
}

void ResourceContextTest::TearDown()
{
    // destroys every resource the test left alive, which is part of what the suite checks
    context.reset();
}

std::vector<uint32_t> ResourceContextTest::ReadBackWords(const GraphicsResource& source, size_t size_bytes)
{
    BufferCreateInfo readbackInfo{};
    readbackInfo.Domain = ResourceDomain::GPUToCPU;
    readbackInfo.Usage = BufferUsageBits::CopyDestination;
    readbackInfo.Size = size_bytes;
    GraphicsResource readback = CreateBufferAndWait(readbackInfo, {});

    std::shared_ptr<ResourceTransferReply> copyReply = context->CopyBufferContents(source, readback);
    EXPECT_EQ(copyReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);

    std::shared_ptr<PointerMessageReply> mapReply = context->MapBuffer(readback, size_bytes, 0u);
    EXPECT_EQ(mapReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    std::vector<uint32_t> words(size_bytes / sizeof(uint32_t));
    if (mapReply->GetPointer() != nullptr)
    {
        std::memcpy(words.data(), mapReply->GetPointer(), size_bytes);
    }
    std::shared_ptr<MessageReply> unmapReply = context->UnmapBuffer(readback, size_bytes, 0u);
    EXPECT_EQ(unmapReply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);

    DestroyAndWait(readback);
    return words;
}

GraphicsResource ResourceContextTest::CreateBufferAndWait(const BufferCreateInfo& create_info, std::span<const RhiBufferResourceData> initial_data)
{
    std::shared_ptr<GraphicsResourceReply> reply = context->CreateBuffer(create_info, initial_data);
    EXPECT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
    return reply->GetResource();
}

void ResourceContextTest::DestroyAndWait(const GraphicsResource& resource)
{
    std::shared_ptr<MessageReply> reply = context->DestroyResource(resource);
    EXPECT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
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
