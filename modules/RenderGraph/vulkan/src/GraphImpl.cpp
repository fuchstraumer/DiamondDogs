#include "GraphImpl.hpp"
#include "CompiledGraph.hpp"
#include "Device.hpp"
#include "LoomGraph.hpp"
#include "ResourceContext.hpp"
#include "ResourceFuture.hpp"
#include <vulkan/vulkan_core.h>
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
        Timeline{ inputs.Device.Handle() }
    {
    }

    GraphImpl::~GraphImpl()
    {
        // the GPU first: nothing below may go while a submit still reads it
        if (LastSignaled > 0u)
        {
            [[maybe_unused]] const bool finished = WaitForTimeline(*this, LastSignaled, k_TeardownTimeoutNs);
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
        for (RowRuntime& row : Rows)
        {
            if (!row.Owned)
            {
                continue;
            }

            const rhi::FutureState created = Resources.Wait(row.Future, k_TeardownTimeoutNs);

            if (created != rhi::FutureState::Complete)
            {
                continue;
            }

            rhi::OperationFuture destroyed = Resources.DestroyResource(row.Future.Get());
            [[maybe_unused]] const rhi::FutureState state = Resources.Wait(destroyed, k_TeardownTimeoutNs);
        }
    }

}
