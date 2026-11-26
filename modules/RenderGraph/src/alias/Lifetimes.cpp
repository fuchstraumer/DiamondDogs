#include "alias/Lifetimes.hpp"
#include "AliasLayout.hpp"
#include "Declarations.hpp"
#include "Records.hpp"
#include "compile/CompiledData.hpp"
#include "freeze/FrozenGraph.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace loom
{

namespace
{

    bool BufferMayAlias(const BufferDeclaration& buffer)
    {
        return buffer.Lifetime == ResourceLifetime::Frame && buffer.HistoryDepth == 0 && buffer.Cpu == CpuAccess::None;
    }

    bool ImageMayAlias(const ImageDeclaration& image)
    {
        const bool frameOnly = image.Lifetime == ResourceLifetime::Frame && image.HistoryDepth == 0;
        const bool gpuOnly = image.Cpu == CpuAccess::None;
        // a resize makes an image sized from a target again, at a new size
        const bool fixedSize = image.Extent.Kind != ExtentSource::Kind::Target;
        return frameOnly && gpuOnly && fixedSize;
    }

    // [row]: a pass whose run policy is not EveryFrame writes it. Its contents then persist across frames (the
    // persistence rule of LM503: a Once pass writes, and every later frame reads), so its bytes cannot be reused.
    std::vector<uint8_t> PersistentlyWritten(const CompiledData& data)
    {
        std::vector<uint8_t> persistent(static_cast<size_t>(data.RowCount), 0u);

        for (const CompiledPass& pass : data.Passes)
        {
            if (pass.Policy == RunPolicy::EveryFrame)
            {
                continue;
            }

            for (int32_t useIndex = pass.FirstUse; useIndex < pass.FirstUse + pass.UseCount; ++useIndex)
            {
                const PassUse& use = data.Uses[useIndex];
                const bool writes = use.Access == ResourceAccess::Write || use.Access == ResourceAccess::ReadWrite;

                if (writes)
                {
                    persistent[use.Resource] = 1u;
                }
            }
        }

        return persistent;
    }

}

    std::vector<AliasCandidate> FindAliasCandidates(const CompiledData& data)
    {
        std::vector<AliasCandidate> candidates;
        const int32_t resourceCount = static_cast<int32_t>(data.Frozen.Resources.size());
        const std::vector<uint8_t> persistent = PersistentlyWritten(data);

        for (int32_t row = 0; row < resourceCount; ++row)
        {
            const FrozenResource& resource = data.Frozen.Resources[row];
            const ResourceSpan& span = data.Spans[row];

            if (resource.Origin != ResourceOrigin::Declared || span.FirstLevel < 0 || persistent[row] != 0u)
            {
                continue;
            }

            AliasClass aliasClass = AliasClass::Invalid;

            switch (resource.Kind)
            {
            case ResourceKind::Buffer:
                if (BufferMayAlias(data.Frozen.Buffers[resource.DeclarationIndex]))
                {
                    aliasClass = AliasClass::Buffer;
                }

                break;
            case ResourceKind::Image:
                if (ImageMayAlias(data.Frozen.Images[resource.DeclarationIndex].Declaration))
                {
                    aliasClass = AliasClass::Image;
                }

                break;
            case ResourceKind::Sampler:
                [[fallthrough]];
            case ResourceKind::Invalid:
                break;
            }

            if (aliasClass == AliasClass::Invalid)
            {
                continue;
            }

            candidates.push_back(AliasCandidate
            {
                .Row = row,
                .FirstLevel = span.FirstLevel,
                .LastLevel = span.LastLevel,
                .Class = aliasClass
            });
        }

        return candidates;
    }

}
