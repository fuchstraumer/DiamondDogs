#include "ShaderManifestFormat.hpp"
#include "bind/Layout.hpp"
#include "compile/Pipelines.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "compile/CompiledData.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestLayout.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
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

                // another code format is copied when its backend exists; Realize refuses a graph it cannot run
                if (spirv)
                {
                    const std::span<const uint32_t> words = instance.SpirvWords();
                    stage.Code.assign(words.begin(), words.end());
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
            }
        }
    }

}
