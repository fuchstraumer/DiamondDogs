#include "GraphImpl.hpp"
#include "CompiledGraph.hpp"
#include "Device.hpp"
#include "LoomGraph.hpp"
#include "ResourceContext.hpp"
#include "ResourceFuture.hpp"
#include "ResourceTypes.hpp"
#include "compile/CompiledData.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstdint>
#include <utility>

namespace loom
{

namespace
{

    // Teardown waits this long for the GPU and for the context. A device that takes longer is lost.
    constexpr uint64_t k_TeardownTimeoutNs = 10'000'000'000ull;

}

    bool WaitForTimeline(const GraphImpl& graph, uint64_t value, uint64_t timeout_ns)
    {
        const VkSemaphore semaphore = graph.Timeline.Handle().As<VkSemaphore>();

        const VkSemaphoreWaitInfo waitInfo
        {
            VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            nullptr,
            0,
            1u,
            &semaphore,
            &value
        };

        return vkWaitSemaphores(graph.VulkanDevice, &waitInfo, timeout_ns) == VK_SUCCESS;
    }

    GraphImpl::GraphImpl(CompiledGraph&& _compiled, const RealizeInputs& inputs) :
        Compiled{ std::move(_compiled) },
        Resources{ inputs.Resources },
        Device{ inputs.Device },
        Sink{ inputs.Sink },
        VulkanDevice{ inputs.Device.Handle().As<VkDevice>() },
        Queue{ inputs.Device.GetGeneralQueue() },
        Timeline{ inputs.Device.Handle() },
        SettleSeconds{ inputs.TargetSettleSeconds }
    {
    }

    GraphImpl::~GraphImpl()
    {
        // the GPU first: nothing below may go while a submit still reads it
        if (LastSignaled > 0u)
        {
            [[maybe_unused]] const bool finished = WaitForTimeline(*this, LastSignaled, k_TeardownTimeoutNs);
        }

        // a present waits on a RenderDone semaphore after the timeline signals, and only an idle queue proves that it
        // finished (VK_EXT_swapchain_maintenance1 present fences would avoid this wait)
        const auto presented = [](const TargetRuntime& target)
        {
            return !target.RenderDone.empty();
        };

        if (std::ranges::any_of(Targets, presented))
        {
            vkDeviceWaitIdle(VulkanDevice);
        }

        // the context reads the timeline while it is registered, so it leaves before the semaphore does
        if (TimelineRegistration.State() != rhi::FutureState::Invalid)
        {
            [[maybe_unused]] const rhi::FutureState registered = Resources.Wait(TimelineRegistration, k_TeardownTimeoutNs);
            rhi::OperationFuture unregister = Resources.UnregisterConsumerTimeline(Timeline.Handle());
            [[maybe_unused]] const rhi::FutureState unregistered = Resources.Wait(unregister, k_TeardownTimeoutNs);
        }

        for (PipelineRuntime& pipeline : Pipelines)
        {
            pipeline.Program.reset();

            if (pipeline.Layout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(VulkanDevice, pipeline.Layout, nullptr);
            }

            for (const VkDescriptorSetLayout setLayout : pipeline.SetLayouts)
            {
                vkDestroyDescriptorSetLayout(VulkanDevice, setLayout, nullptr);
            }
        }

        if (DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(VulkanDevice, DescriptorPool, nullptr);
        }

        // an object that is still being created must exist before it can be destroyed
        const auto destroyOwned = [this](const rhi::ResourceFuture<GraphicsResource>& future)
        {
            const rhi::FutureState created = Resources.Wait(future, k_TeardownTimeoutNs);

            if (created != rhi::FutureState::Complete)
            {
                return;
            }

            rhi::OperationFuture destroyed = Resources.DestroyResource(future.Get());
            [[maybe_unused]] const rhi::FutureState state = Resources.Wait(destroyed, k_TeardownTimeoutNs);
        };

        // an OfTarget image that no target extent made yet has no future
        const auto destroyMade = [&destroyOwned](const rhi::ResourceFuture<GraphicsResource>& future)
        {
            if (future.State() != rhi::FutureState::Invalid)
            {
                destroyOwned(future);
            }
        };

        for (const RowRuntime& row : Rows)
        {
            if (row.Owned)
            {
                destroyMade(row.Future);
            }
        }

        // the graph makes every history copy
        for (const rhi::ResourceFuture<GraphicsResource>& copy : ExtraCopies)
        {
            destroyMade(copy);
        }

        // step 7: the images of a resize that did not swap in, the old images, and the LoomFrame buffer
        for (const NextImage& next : NextImages)
        {
            destroyMade(next.Future);
        }

        for (const rhi::ResourceFuture<GraphicsResource>& retired : Retired)
        {
            destroyMade(retired);
        }

        destroyMade(LoomFrameBuffer);

        for (const rhi::OperationFuture& destroy : Destroys)
        {
            [[maybe_unused]] const rhi::FutureState state = Resources.Wait(destroy, k_TeardownTimeoutNs);
        }
    }

    const rhi::ResourceFuture<GraphicsResource>& FutureOfCopy(const GraphImpl& graph, int32_t copy) noexcept
    {
        const int32_t rowCount = graph.Compiled.RowCount();
        return copy < rowCount ? graph.Rows[copy].Future : graph.ExtraCopies[copy - rowCount];
    }

    rhi::ResourceFuture<GraphicsResource>& FutureOfCopy(GraphImpl& graph, int32_t copy) noexcept
    {
        const int32_t rowCount = graph.Compiled.RowCount();
        return copy < rowCount ? graph.Rows[copy].Future : graph.ExtraCopies[copy - rowCount];
    }

    VkImage ImageOfCopy(const GraphImpl& graph, int32_t copy) noexcept
    {
        const CompiledData& data = graph.Compiled.Data();

        if (IsFrameTargetRow(data, copy) && copy < data.RowCount)
        {
            const TargetRuntime& target = graph.Targets[FrameTargetOfRow(data, copy)];
            return target.Acquired.has_value() ? target.Acquired->Image.As<VkImage>() : VK_NULL_HANDLE;
        }

        return reinterpret_cast<VkImage>(FutureOfCopy(graph, copy).Get().VkHandle);
    }

    VkImageView ViewOfCopy(const GraphImpl& graph, int32_t copy) noexcept
    {
        const CompiledData& data = graph.Compiled.Data();

        if (IsFrameTargetRow(data, copy) && copy < data.RowCount)
        {
            const TargetRuntime& target = graph.Targets[FrameTargetOfRow(data, copy)];
            return target.Acquired.has_value() ? target.Acquired->View.As<VkImageView>() : VK_NULL_HANDLE;
        }

        return reinterpret_cast<VkImageView>(FutureOfCopy(graph, copy).Get().VkViewHandle);
    }

    int32_t SetInstanceOf(const PassRuntime& pass, int32_t frame_slot, int64_t frame_index) noexcept
    {
        if (pass.Phases == 1)
        {
            return frame_slot;
        }
        return (frame_slot * pass.Phases) + static_cast<int32_t>(frame_index % pass.Phases);
    }

}
