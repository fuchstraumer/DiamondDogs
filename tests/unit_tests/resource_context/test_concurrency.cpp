#include "ResourceContextFixture.hpp"
#include <atomic>
#include <thread>

// ResourceContext is multi-writer, single-reader: any thread may send messages. this drives every path at once
TEST_F(ResourceContextTest, ManyThreadsCreateUploadAndDestroy)
{
    constexpr uint32_t k_Threads = 4u;
    constexpr uint32_t k_BuffersPerThread = 16u;
    std::atomic<uint32_t> failures{ 0u };

    std::vector<std::thread> threads;
    for (uint32_t t = 0; t < k_Threads; ++t)
    {
        threads.emplace_back([this, t, &failures]()
        {
            for (uint32_t i = 0; i < k_BuffersPerThread; ++i)
            {
                const std::vector<uint32_t> words = MakeWordPattern(256u, t * 1000u + i);
                const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
                BufferCreateInfo info{};
                // alternate between the host path and the transfer path
                info.Domain = (i % 2u == 0u) ? ResourceDomain::GPUOnly : ResourceDomain::CPUToGPU;
                info.Usage = BufferUsageBits::ComputeShaderSRV;
                info.Size = data.Data.size();

                std::shared_ptr<GraphicsResourceReply> reply = context->CreateBuffer(info, std::span{ &data, 1u });
                if (reply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
                {
                    ++failures;
                    continue;
                }
                std::shared_ptr<MessageReply> destroyReply = context->DestroyResource(reply->GetResource());
                if (destroyReply->WaitForCompletion(k_TimeoutNs) != MessageReply::Status::Completed)
                {
                    ++failures;
                }
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }
    EXPECT_EQ(failures.load(), 0u);
}

TEST_F(ResourceContextTest, ShutdownWithPendingWorkCompletesEveryReply)
{
    // the context is destroyed while messages are still queued. every reply must still finish, and every resource must
    // be destroyed before the device is (the validation layer reports leaks at vkDestroyDevice)
    std::vector<std::shared_ptr<GraphicsResourceReply>> replies;
    const std::vector<uint32_t> words = MakeWordPattern(1024u, 42u);
    const RhiBufferResourceData data{ std::as_bytes(std::span{ words }), 0u };
    for (uint32_t i = 0; i < 32u; ++i)
    {
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::GPUOnly;
        info.Usage = BufferUsageBits::ComputeShaderSRV;
        info.Size = data.Data.size();
        replies.push_back(context->CreateBuffer(info, std::span{ &data, 1u }));
    }

    context.reset();
    for (const std::shared_ptr<GraphicsResourceReply>& reply : replies)
    {
        EXPECT_TRUE(reply->IsCompleted());
    }
}
