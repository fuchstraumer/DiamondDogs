#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_GRAPH_IMPL_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_GRAPH_IMPL_HPP
#include "CommandPool.hpp"
#include "CompiledGraph.hpp"
#include "FramePlan.hpp"
#include "GpuTimers.hpp"
#include "LoomGraph.hpp"
#include "ParamsStore.hpp"
#include "ResourceFuture.hpp"
#include "RhiTypes.hpp"
#include "Semaphore.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderProgram.hpp"
#include "compile/CompiledData.hpp"
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class ResourceContext;

// The Vulkan backend's state. Realize.cpp builds it, Tick.cpp runs it, GraphImpl.cpp tears it down. Every array is sized
// at Realize, so a Tick allocates nothing.
namespace loom
{
    class DiagnosticSink;

    // One resource row. Future is the graph's own create, or the last Supply; it is empty for an import that nothing
    // supplied yet. Reported holds the epoch whose failure (LM701, LM702) the sink already has, so each is reported once.
    struct RowRuntime
    {
        rhi::ResourceFuture<GraphicsResource> Future;
        bool Owned{ false };
        bool KindMismatch{ false };     // LM701 for this epoch: the passes treat the row as Failed
        uint32_t Reported{ 0u };
    };

    // The Vulkan objects of one pipeline. The set layouts are one per group, 0 to the highest group the slots use.
    struct PipelineRuntime
    {
        std::vector<VkDescriptorSetLayout> SetLayouts;
        VkPipelineLayout Layout{ VK_NULL_HANDLE };
        rhi::PipelineLayoutHandle LayoutHandle;
        std::unique_ptr<rhi::ShaderProgram> Program;
        rhi::ShaderStageFlags PushStages{ rhi::ShaderStageFlags::None };
        uint32_t PushBytes{ 0u };
    };

    // One pass's descriptor sets: Sets[frame_slot * GroupCount + group]. WrittenEpochs[frame_slot * SlotCount + slot]
    // is the epoch of the slot's row when the set was last written; 0 means never.
    struct PassRuntime
    {
        int32_t GroupCount{ 0 };
        std::vector<VkDescriptorSet> Sets;
        std::vector<uint32_t> WrittenEpochs;
    };

    struct FrameSlotRuntime
    {
        rhi::CommandPool Pool;
        uint64_t SignaledValue{ 0u };   // the timeline value of the last submit that used this slot
    };

    struct GraphImpl
    {
        GraphImpl(CompiledGraph&& _compiled, const RealizeInputs& inputs);
        ~GraphImpl();

        CompiledGraph Compiled;
        ResourceContext& Resources;
        const rhi::Device& Device;
        DiagnosticSink& Sink;
        VkDevice VulkanDevice{ VK_NULL_HANDLE };
        rhi::QueueHandle Queue;

        rhi::TimelineSemaphore Timeline;
        uint64_t LastSignaled{ 0u };
        rhi::OperationFuture TimelineRegistration;

        std::vector<RowRuntime> Rows;
        std::vector<PipelineRuntime> Pipelines;
        std::vector<PassRuntime> Passes;
        std::vector<FrameSlotRuntime> FrameSlots;
        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        int64_t FrameIndex{ 0 };

        // the core's state. States[row] is what the last poll saw.
        std::vector<ResourceState> States;
        std::vector<ResourceSync> Sync;
        std::vector<PassControl> Controls;
        ParamsStore Params;
        FramePlan Plan;

        // Tick's scratch, sized at Realize
        std::vector<rhi::SemaphoreSubmitInfo> WaitScratch;
        std::vector<VkWriteDescriptorSet> WriteScratch;
        std::vector<VkDescriptorBufferInfo> BufferInfoScratch;
        std::vector<rhi::DescriptorSetHandle> SetScratch;

        std::optional<rhi::GpuTimers> Timers;
    };

    // GraphImpl.cpp: blocks until the graph's timeline reaches `value`, or the timeout passes. False on a timeout.
    [[nodiscard]] bool WaitForTimeline(const GraphImpl& graph, uint64_t value, uint64_t timeout_ns);

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_GRAPH_IMPL_HPP
