#include "ShaderManifestFormat.hpp"
#include "bind/Layout.hpp"
#include "compile/Pipelines.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "compile/CompiledData.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestLayout.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Copies each pipeline's code out of the bundle (G4), with the facts a backend needs to build its layouts: the
// workgroup and the push constant range. A pipeline's descriptor layout comes from the slots of its first pass.
namespace loom::compile
{

namespace
{

    int32_t FirstPassOf(const CompiledData& data, int32_t pipeline)
    {
        for (int32_t pass = 0; pass < static_cast<int32_t>(data.LaidOut.Passes.size()); ++pass)
        {
            if (data.LaidOut.Passes[pass].Pipeline == pipeline)
            {
                return pass;
            }
        }

        return -1;
    }

    // A draw's instances are the vertex one, then the fragment one (bind::PassInstances).
    const std::string& EntryPointNameOf(const FrozenPass& pass, size_t instance_index)
    {
        return instance_index == 0u ? pass.Shader.EntryPoint : pass.Shader.FragmentEntryPoint;
    }

}

    void CopyPipelines(const ResolvedGraph& resolved, CompiledData& data)
    {
        const bool spirv = data.CodeFormat == lodestone::ShaderCodeFormat::Spirv;
        const bool wgsl = data.CodeFormat == lodestone::ShaderCodeFormat::Wgsl;
        data.Pipelines.resize(data.LaidOut.Pipelines.size());

        for (int32_t pipelineIndex = 0; pipelineIndex < static_cast<int32_t>(data.Pipelines.size()); ++pipelineIndex)
        {
            CompiledPipeline& pipeline = data.Pipelines[pipelineIndex];
            pipeline.LayoutPass = FirstPassOf(data, pipelineIndex);

            if (pipeline.LayoutPass < 0)
            {
                continue;
            }

            const FrozenPass& pass = data.Frozen.Passes[pipeline.LayoutPass];
            const std::vector<lodestone::manifest::EntryPointInstanceView> instances =
                bind::PassInstances(resolved.Passes[pipeline.LayoutPass]);

            for (size_t instanceIndex = 0u; instanceIndex < instances.size(); ++instanceIndex)
            {
                const lodestone::manifest::EntryPointInstanceView& instance = instances[instanceIndex];
                CompiledStage& stage = pipeline.Stages.emplace_back();
                stage.Stage = instance.Stage();
                stage.EntryPoint = EntryPointNameOf(pass, instanceIndex);
                assert((spirv && !wgsl) || (!spirv && wgsl));

                if (spirv)
                {
                    const std::span<const std::byte> bytes = std::as_bytes(instance.SpirvWords());
                    stage.ShaderCode.assign(bytes.begin(), bytes.end());
                }
                else if (wgsl)
                {
                    const std::string_view text = instance.Source();
                    const std::span<const std::byte> bytes = std::as_bytes(std::span{ text.data(), text.size() });
                    stage.ShaderCode.assign(bytes.begin(), bytes.end());
                }
            }

            if (instances.empty())
            {
                continue;
            }

            pipeline.Workgroup = instances.front().Workgroup();

            // Bind merged these instances already, so the merge succeeds here
            const lodestone::manifest::ManifestResult<lodestone::manifest::MergedLayoutView> merged =
                lodestone::manifest::MergedLayoutView::Merge(instances);

            if (!merged.has_value())
            {
                continue;
            }

            const std::optional<lodestone::manifest::MergedPushConstants> pushConstants = merged->PushConstants();

            if (pushConstants.has_value())
            {
                pipeline.PushConstantStages = pushConstants->Stages;
                pipeline.PushConstantBytes = pushConstants->Constants.ByteSize();
                pipeline.ConstantsDelivery = lodestone::EntryPointConstantsDelivery::PushConstant;
                pipeline.ConstantsBytes = pushConstants->Constants.ByteSize();
            }
            else
            {
                const lodestone::manifest::EntryPointConstantsView constants = instances.front().Constants();

                if (constants.Delivery() == lodestone::EntryPointConstantsDelivery::UniformBuffer)
                {
                    const lodestone::manifest::ResolvedResource constantResource =
                        instances.front().Layout()[constants.BindingPosition()];
                    pipeline.ConstantsDelivery = lodestone::EntryPointConstantsDelivery::UniformBuffer;
                    pipeline.ConstantsBytes = constants.ByteSize();
                    pipeline.ConstantsGroup = constantResource.PlacementValue().Word0;
                    pipeline.ConstantsBinding = constantResource.PlacementValue().Word1;
                }
            }
        }
    }

}
