#pragma once
#ifndef DIAMOND_DOGS_LOOM_VULKAN_GRAPH_IMPL_HPP
#define DIAMOND_DOGS_LOOM_VULKAN_GRAPH_IMPL_HPP
#include "CommandPool.hpp"
#include "CompiledGraph.hpp"
#include "FramePlan.hpp"
#include "FrameTarget.hpp"
#include "LoomFrame.hpp"
#include "RasterRecording.hpp"
#include "TargetExtents.hpp"
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
        bool ImportChecked{ false };    // Avoids repeated CheckImport calls on each tick
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

    // One pass's descriptor sets, one set of groups per (frame slot, ring phase) (spec R3): a pass that binds history
    // rings has Phases = the least common multiple of their copy counts, else 1. With
    // instance = frame_slot * Phases + phase: Sets[instance * GroupCount + group], and WrittenEpochs[instance * SlotCount
    // + slot] is the epoch of the slot's row when that set was last written; 0 means never.
    struct PassRuntime
    {
        int32_t GroupCount{ 0 };
        int32_t Phases{ 1 };
        std::vector<VkDescriptorSet> Sets;
        std::vector<uint32_t> WrittenEpochs;
        std::vector<uint64_t> InstanceGeneration;   // lets us skip descriptor writes when nothing is updated
    };

    struct FrameSlotRuntime
    {
        rhi::CommandPool Pool;
        uint64_t SignaledValue{ 0u };   // the timeline value of the last submit that used this slot
    };

    // One frame target (step 7). Target is the caller's object. Acquired is this Tick's image, or nothing. Presented:
    // the graph has presented to it, so its teardown waits for the presents.
    struct TargetRuntime
    {
        FrameTarget* Target{ nullptr };
        TargetExtents Extents;
        std::optional<AcquiredTarget> Acquired;
        bool Presented{ false };
    };

    // A settled resize's new image for one copy of an OfTarget image. Every new image of a target swaps in on one
    // Tick, once all of them exist (step 7, round 2 Q1).
    struct NextImage
    {
        int32_t Copy{ -1 };
        rhi::ResourceFuture<GraphicsResource> Future;
    };

    struct GraphImpl;

    // The object of one copy (CompiledData::CopyRows) of a resource row: a row's first copy is the row's own future,
    // and a ring's other copies follow every row (CompiledData::RowCount). Not for a frame-target row.
    [[nodiscard]] const rhi::ResourceFuture<GraphicsResource>& FutureOfCopy(const GraphImpl& graph, int32_t copy) noexcept;
    [[nodiscard]] rhi::ResourceFuture<GraphicsResource>& FutureOfCopy(GraphImpl& graph, int32_t copy) noexcept;

    // The image and view of an image copy: a resource row's object, or a frame target's acquired image
    [[nodiscard]] VkImage ImageOfCopy(const GraphImpl& graph, int32_t copy) noexcept;
    [[nodiscard]] VkImageView ViewOfCopy(const GraphImpl& graph, int32_t copy) noexcept;

    // The instance of a pass's sets that frame `frame_index` uses.
    [[nodiscard]] int32_t SetInstanceOf(const PassRuntime& pass, int32_t frame_slot, int64_t frame_index) noexcept;

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

        std::vector<RowRuntime> Rows;                   // [resource row]; frame-target rows have none
        // the history copies after each row's first: ExtraCopies[copy - RowCount] (CompiledData::ExtraCopyBase)
        std::vector<rhi::ResourceFuture<GraphicsResource>> ExtraCopies;
        std::vector<PipelineRuntime> Pipelines;
        std::vector<PassRuntime> Passes;
        std::vector<FrameSlotRuntime> FrameSlots;
        VkDescriptorPool DescriptorPool{ VK_NULL_HANDLE };
        int64_t FrameIndex{ 0 };

        // the core's state. States[row] is what the last poll saw; Sync has one entry per copy.
        std::vector<ResourceState> States;
        std::vector<ResourceSync> Sync;
        std::vector<PassControl> Controls;
        ParamsStore Params;
        FramePlan Plan;

        // Tick's scratch, sized at Realize
        std::vector<rhi::SemaphoreSubmitInfo> WaitScratch;
        std::vector<VkWriteDescriptorSet> WriteScratch;
        std::vector<VkDescriptorBufferInfo> BufferInfoScratch;
        std::vector<VkDescriptorImageInfo> ImageInfoScratch;
        std::vector<rhi::ImageBarrier> ImageBarrierScratch;

        std::optional<rhi::GpuTimers> Timers;
        uint32_t CreatedRowCount{ 0u };
        bool AllRowsSettled{ false };
        uint64_t SupplyGeneration{ 1u };

        // Step 7: frame targets, the OfTarget images that a settled resize is making, the objects that wait to be
        // destroyed once they exist, and the destroys in flight
        std::vector<TargetRuntime> Targets;             // [frame target]
        double SettleSeconds{ 0.0 };
        std::vector<NextImage> NextImages;
        std::vector<rhi::ResourceFuture<GraphicsResource>> Retired;
        std::vector<rhi::OperationFuture> Destroys;

        // Draw passes: the dynamic-state entry points, and the LoomFrame buffer (one buffer, updated at the start of
        // each frame's command buffer)
        RasterFunctions Raster;
        rhi::ResourceFuture<GraphicsResource> LoomFrameBuffer;
        bool HasLoomFrame{ false };
        LoomFrameConstants FrameConstants{};
        double TimeSeconds{ 0.0 };
        float DeltaSeconds{ 0.0f };
    };

    // GraphImpl.cpp: blocks until the graph's timeline reaches `value`, or the timeout passes. False on a timeout.
    [[nodiscard]] bool WaitForTimeline(const GraphImpl& graph, uint64_t value, uint64_t timeout_ns);

}

#endif // !DIAMOND_DOGS_LOOM_VULKAN_GRAPH_IMPL_HPP
