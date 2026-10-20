#include "ResourceContextFixture.hpp"
#include <atomic>
#include <chrono>
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

// the worker used to poll with a backoff sleeper that grew to 500ms (with 30% jitter), so the first request after an
// idle stretch waited for the rest of that sleep. it blocks on the queue now, so a request after idling is served at
// once. three idle rounds, so that the old worker would almost surely have been caught mid-sleep in one of them
TEST_F(ResourceContextTest, RequestAfterIdleIsServedPromptly)
{
    constexpr int32_t k_IdleRounds = 3;
    constexpr std::chrono::milliseconds k_IdleTime{ 800 };
    constexpr std::chrono::milliseconds k_MaxLatency{ 100 };

    for (int32_t round = 0; round < k_IdleRounds; ++round)
    {
        std::this_thread::sleep_for(k_IdleTime);

        // no initial data, so the request never reaches the transfer system and only measures the context's worker
        BufferCreateInfo info{};
        info.Domain = ResourceDomain::CPUToGPU;
        info.Usage = BufferUsageBits::ComputeShaderSRV;
        info.Size = 256u;
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::shared_ptr<GraphicsResourceReply> reply = context->CreateBuffer(info, {});
        ASSERT_EQ(reply->WaitForCompletion(k_TimeoutNs), MessageReply::Status::Completed);
        const std::chrono::steady_clock::duration latency = std::chrono::steady_clock::now() - start;

        EXPECT_LT(latency, k_MaxLatency) << "round " << round << " took "
            << std::chrono::duration_cast<std::chrono::milliseconds>(latency).count() << "ms";
        DestroyAndWait(reply->GetResource());
    }
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
