#include "Realize.hpp"
#include "GraphImpl.hpp"
#include "VulkanMapping.hpp"
#include "CommandPool.hpp"
#include "CompiledGraph.hpp"
#include "Device.hpp"
#include "LoomGraph.hpp"
#include "ResourceContext.hpp"
#include "RhiResourceFlags.hpp"
#include "ResourceTypes.hpp"
#include "ShaderProgram.hpp"
#include "ShaderTypes.hpp"
#include "compile/CompiledData.hpp"
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

// Realize: checks that this backend can run the graph, then creates what the graph runs on. Loom builds its own
// layouts and shader objects from the compiled slot table and the copied SPIR-V (step 5 fork F1), so nothing here
// reads the bundle.
namespace loom
{

namespace
{

    bool SlotIsSupported(const BindingSlot& slot)
    {
        const bool buffer = slot.Kind == lodestone::BindingKind::UniformBuffer ||
                            slot.Kind == lodestone::BindingKind::StorageBuffer;
        return buffer && slot.ArrayCount == 1u;
    }

    // Every GPU use of a buffer row, in RhiResources' usage bits, and the CPU copy that its CpuAccess needs.
    BufferUsageBits UsageOf(const CompiledData& data, int32_t row, CpuAccess cpu)
    {
        BufferUsageBits usage = BufferUsageBits::Invalid;

        for (const PassUse& use : data.Uses)
        {
            if (use.Resource != row)
            {
                continue;
            }

            const bool compute = (use.Scope.Stages & SyncStage::Compute) != SyncStage::None;
            const bool uniform = (use.Scope.Accesses & SyncAccess::UniformRead) != SyncAccess::None;
            const bool storage = (use.Scope.Accesses & (SyncAccess::ShaderRead | SyncAccess::ShaderWrite)) != SyncAccess::None;
            const bool indirect = (use.Scope.Accesses & SyncAccess::IndirectRead) != SyncAccess::None;

            if (compute && uniform)
            {
                usage |= BufferUsageBits::ComputeShaderUBO;
            }

            if (compute && storage)
            {
                usage |= BufferUsageBits::ComputeShaderUAV;
            }

            if (indirect)
            {
                usage |= BufferUsageBits::IndirectCompute;
            }
        }

        if (cpu == CpuAccess::Readback)
        {
            usage |= BufferUsageBits::CopySource;
        }

        if (cpu == CpuAccess::Upload)
        {
            usage |= BufferUsageBits::CopyDestination;
        }

        return usage;
    }

    int32_t GroupCountOf(const CompiledData& data, int32_t pass)
    {
        const BoundPass& bound = data.BoundPasses[pass];
        int32_t groups = 0;

        for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
        {
            groups = std::max(groups, static_cast<int32_t>(data.Slots[slotIndex].Group) + 1);
        }

        return groups;
    }

    // PipelineMode::ComputePipelines: the same code and layout as a VkPipeline. CheckSupported let only dispatches
    // through, so the pipeline has one compute stage.
    RealizeError CreateComputePipeline(GraphImpl& graph, const CompiledPipeline& compiled, PipelineRuntime& pipeline)
    {
        const CompiledStage& stage = compiled.Stages.front();

        const VkShaderModuleCreateInfo moduleInfo
        {
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            stage.Code.size() * sizeof(uint32_t),
            stage.Code.data()
        };

        VkShaderModule module = VK_NULL_HANDLE;

        if (vkCreateShaderModule(graph.VulkanDevice, &moduleInfo, nullptr, &module) != VK_SUCCESS)
        {
            return RealizeError::PipelineCreateFailed;
        }

        const VkComputePipelineCreateInfo pipelineInfo
        {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = VkPipelineShaderStageCreateInfo
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = module,
                .pName = stage.EntryPoint.c_str(),
            },
            .layout = pipeline.Layout,
        };

        const VkResult result = vkCreateComputePipelines(graph.VulkanDevice, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr,
                                                         &pipeline.Pipeline);
        vkDestroyShaderModule(graph.VulkanDevice, module, nullptr);

        if (result != VK_SUCCESS)
        {
            return RealizeError::PipelineCreateFailed;
        }

        return RealizeError::Invalid;
    }

    RealizeError CreatePipeline(GraphImpl& graph, const CompiledPipeline& compiled, PipelineRuntime& pipeline)
    {
        const CompiledData& data = graph.Compiled.Data();
        const BoundPass& bound = data.BoundPasses[compiled.LayoutPass];
        const int32_t groups = GroupCountOf(data, compiled.LayoutPass);

        for (int32_t group = 0; group < groups; ++group)
        {
            std::vector<VkDescriptorSetLayoutBinding> bindings;

            for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
            {
                const BindingSlot& slot = data.Slots[slotIndex];

                if (static_cast<int32_t>(slot.Group) != group)
                {
                    continue;
                }

                // CheckSupported let only kinds with a descriptor type through
                const VkDescriptorType type = *ToDescriptorType(slot.Kind, data.SlotWritable[slotIndex] != 0u);
                bindings.push_back(VkDescriptorSetLayoutBinding{ slot.Binding, type, slot.ArrayCount, ToVkShaderStages(slot.Stages), nullptr });
            }

            const VkDescriptorSetLayoutCreateInfo layoutInfo
            {
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                nullptr,
                0,
                static_cast<uint32_t>(bindings.size()),
                bindings.data()
            };

            VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;

            if (vkCreateDescriptorSetLayout(graph.VulkanDevice, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS)
            {
                return RealizeError::DescriptorLayoutFailed;
            }

            pipeline.SetLayouts.push_back(setLayout);
        }

        // Slang puts every stage's push constant block at offset 0, so one range covers the pipeline
        const VkPushConstantRange pushRange{ ToVkShaderStages(compiled.PushConstantStages), 0u, compiled.PushConstantBytes };
        const bool hasPush = compiled.PushConstantBytes > 0u;

        const VkPipelineLayoutCreateInfo pipelineLayoutInfo
        {
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(pipeline.SetLayouts.size()),
            pipeline.SetLayouts.data(),
            hasPush ? 1u : 0u,
            hasPush ? &pushRange : nullptr
        };

        if (vkCreatePipelineLayout(graph.VulkanDevice, &pipelineLayoutInfo, nullptr, &pipeline.Layout) != VK_SUCCESS)
        {
            return RealizeError::PipelineLayoutFailed;
        }

        pipeline.LayoutHandle.Set(pipeline.Layout);
        pipeline.PushStages = ToRhiShaderStages(compiled.PushConstantStages);
        pipeline.PushBytes = compiled.PushConstantBytes;

        if (graph.Mode == PipelineMode::ComputePipelines)
        {
            return CreateComputePipeline(graph, compiled, pipeline);
        }

        // ShaderBinaryOptions keeps spans, so this storage outlives the create call
        std::vector<rhi::DescriptorSetLayoutHandle> layoutHandles(pipeline.SetLayouts.size());

        for (size_t index = 0u; index < pipeline.SetLayouts.size(); ++index)
        {
            layoutHandles[index].Set(pipeline.SetLayouts[index]);
        }

        std::vector<rhi::PushConstantRange> pushRanges;

        if (hasPush)
        {
            pushRanges.push_back(rhi::PushConstantRange{ pipeline.PushStages, 0u, compiled.PushConstantBytes });
        }

        std::vector<rhi::ShaderBinaryOptions> options;
        options.reserve(compiled.Stages.size());

        for (const CompiledStage& stage : compiled.Stages)
        {
            options.emplace_back(std::span<const uint32_t>{ stage.Code }, ToRhiShaderStages(stage.Stage), stage.EntryPoint,
                                 std::span<rhi::PushConstantRange>{ pushRanges },
                                 std::span<rhi::DescriptorSetLayoutHandle>{ layoutHandles });
        }

        if (rhi::ShaderProgram::CreateFromBinary(graph.Device.Handle(), options, pipeline.Program).IsFailure())
        {
            return RealizeError::ShaderCreateFailed;
        }

        return RealizeError::Invalid;
    }

}

    RealizeError CheckSupported(const CompiledData& data)
    {
        if (data.CodeFormat != lodestone::ShaderCodeFormat::Spirv)
        {
            return RealizeError::CodeNotSpirv;
        }

        for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(data.Frozen.Passes.size()); ++passIndex)
        {
            const FrozenPass& pass = data.Frozen.Passes[passIndex];

            if (pass.Kind != PassKind::Dispatch)
            {
                return RealizeError::PassKindNotYet;
            }

            if (pass.Policy == RunPolicy::WhenInputsChange || pass.Policy == RunPolicy::OnTrigger)
            {
                return RealizeError::RunPolicyNotYet;
            }

            if (data.Passes[passIndex].ReadsHistory)
            {
                return RealizeError::HistoryNotYet;
            }
        }

        if (!data.Frozen.FrameTargets.empty())
        {
            return RealizeError::ResourceKindNotYet;
        }

        for (const FrozenResource& row : data.Frozen.Resources)
        {
            if (row.Kind != ResourceKind::Buffer)
            {
                return RealizeError::ResourceKindNotYet;
            }

            if (row.Origin == ResourceOrigin::Declared && data.Frozen.Buffers[row.DeclarationIndex].HistoryDepth > 0)
            {
                return RealizeError::HistoryNotYet;
            }
        }

        for (const BindingSlot& slot : data.Slots)
        {
            if (slot.Form != lodestone::PlacementKind::Bound)
            {
                return RealizeError::PlacementNotBound;
            }

            if (!SlotIsSupported(slot))
            {
                return RealizeError::BindingNotYet;
            }
        }

        return RealizeError::Invalid;
    }

    RealizeError CreatePipelines(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        graph.Pipelines.resize(data.Pipelines.size());

        for (size_t index = 0u; index < data.Pipelines.size(); ++index)
        {
            const CompiledPipeline& compiled = data.Pipelines[index];

            if (compiled.LayoutPass < 0)
            {
                continue;
            }

            const RealizeError error = CreatePipeline(graph, compiled, graph.Pipelines[index]);

            if (error != RealizeError::Invalid)
            {
                return error;
            }
        }

        return RealizeError::Invalid;
    }

    RealizeError CreateDescriptors(GraphImpl& graph, int32_t frame_slots)
    {
        const CompiledData& data = graph.Compiled.Data();
        const int32_t passCount = static_cast<int32_t>(data.Passes.size());
        graph.Passes.resize(passCount);

        uint32_t setCount = 0u;
        std::array<uint32_t, 2> typeCounts{};       // uniform buffers, storage buffers

        for (int32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const int32_t pipeline = data.LaidOut.Passes[passIndex].Pipeline;

            if (pipeline == k_NoPipeline)
            {
                continue;
            }

            const BoundPass& bound = data.BoundPasses[passIndex];
            PassRuntime& pass = graph.Passes[passIndex];
            pass.GroupCount = static_cast<int32_t>(graph.Pipelines[pipeline].SetLayouts.size());
            pass.WrittenEpochs.assign(static_cast<size_t>(frame_slots) * bound.SlotCount, 0u);
            setCount += static_cast<uint32_t>(pass.GroupCount * frame_slots);

            for (int32_t slotIndex = bound.FirstSlot; slotIndex < bound.FirstSlot + bound.SlotCount; ++slotIndex)
            {
                const bool uniform = data.Slots[slotIndex].Kind == lodestone::BindingKind::UniformBuffer;
                typeCounts[uniform ? 0u : 1u] += static_cast<uint32_t>(frame_slots);
            }
        }

        if (setCount == 0u)
        {
            return RealizeError::Invalid;
        }

        std::vector<VkDescriptorPoolSize> poolSizes;

        if (typeCounts[0] > 0u)
        {
            poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, typeCounts[0] });
        }

        if (typeCounts[1] > 0u)
        {
            poolSizes.push_back(VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, typeCounts[1] });
        }

        const VkDescriptorPoolCreateInfo poolInfo
        {
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            nullptr,
            0,
            setCount,
            static_cast<uint32_t>(poolSizes.size()),
            poolSizes.data()
        };

        if (vkCreateDescriptorPool(graph.VulkanDevice, &poolInfo, nullptr, &graph.DescriptorPool) != VK_SUCCESS)
        {
            return RealizeError::DescriptorPoolFailed;
        }

        for (int32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            PassRuntime& pass = graph.Passes[passIndex];

            if (pass.GroupCount == 0)
            {
                continue;
            }

            const PipelineRuntime& pipeline = graph.Pipelines[data.LaidOut.Passes[passIndex].Pipeline];
            pass.Sets.resize(static_cast<size_t>(pass.GroupCount) * frame_slots, VK_NULL_HANDLE);

            for (int32_t frameSlot = 0; frameSlot < frame_slots; ++frameSlot)
            {
                const VkDescriptorSetAllocateInfo allocateInfo
                {
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    nullptr,
                    graph.DescriptorPool,
                    static_cast<uint32_t>(pass.GroupCount),
                    pipeline.SetLayouts.data()
                };

                VkDescriptorSet* sets = pass.Sets.data() + static_cast<size_t>(frameSlot) * pass.GroupCount;

                if (vkAllocateDescriptorSets(graph.VulkanDevice, &allocateInfo, sets) != VK_SUCCESS)
                {
                    return RealizeError::DescriptorPoolFailed;
                }
            }
        }

        return RealizeError::Invalid;
    }

    RealizeError CreateFrameSlots(GraphImpl& graph, int32_t frame_slots)
    {
        const uint32_t family = graph.Device.GetQueueFamilyIndices().Graphics;
        graph.FrameSlots.reserve(static_cast<size_t>(frame_slots));

        for (int32_t frameSlot = 0; frameSlot < frame_slots; ++frameSlot)
        {
            FrameSlotRuntime& slot = graph.FrameSlots.emplace_back(
                FrameSlotRuntime{ rhi::CommandPool(graph.Device.Handle(), rhi::CommandPool::Type::Compute, family), 0u });

            if (slot.Pool.AllocateCommandBuffers(1u).IsFailure())
            {
                return RealizeError::CommandPoolFailed;
            }
        }

        return RealizeError::Invalid;
    }

    void CreateResources(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        const int32_t rowCount = static_cast<int32_t>(data.Frozen.Resources.size());
        graph.Rows.resize(rowCount);
        graph.States.assign(rowCount, ResourceState{ .Readiness = ResourceReadiness::Pending, .Epoch = 0u });

        for (int32_t row = 0; row < rowCount; ++row)
        {
            const FrozenResource& frozen = data.Frozen.Resources[row];

            // an import waits for Supply; CheckSupported let only buffers through
            if (frozen.Origin != ResourceOrigin::Declared)
            {
                continue;
            }

            const BufferDeclaration& declaration = data.Frozen.Buffers[frozen.DeclarationIndex];

            BufferCreateInfo info{};
            info.Domain = ResourceDomain::GPUOnly;
            info.Usage = UsageOf(data, row, declaration.Cpu);
            info.Size = static_cast<size_t>(data.Sized.Resources[row].ByteSize);
            info.Name = frozen.Key.Name;

            graph.Rows[row].Future = graph.Resources.CreateBuffer(info, {});
            graph.Rows[row].Owned = true;
            graph.States[row].Epoch = 1u;
        }
    }

    void PrepareCoreState(GraphImpl& graph)
    {
        const CompiledData& data = graph.Compiled.Data();
        PrepareFramePlan(graph.Compiled, graph.Plan);
        graph.Params.Prepare(graph.Compiled);
        graph.Sync.assign(data.Frozen.Resources.size(), ResourceSync{});
        graph.Controls.assign(data.Passes.size(), PassControl{});

        // the most a Tick writes: every barrier of a plan, one wait per row, every slot of every pass
        graph.BarrierScratch.reserve(data.Uses.size());
        graph.WaitScratch.reserve(data.Frozen.Resources.size());
        graph.WriteScratch.reserve(data.Slots.size());
        graph.BufferInfoScratch.reserve(data.Slots.size());
        graph.SetScratch.reserve(8u);

        for (const PassRuntime& pass : graph.Passes)
        {
            graph.SetScratch.reserve(std::max(graph.SetScratch.capacity(), static_cast<size_t>(pass.GroupCount)));
        }
    }

}
