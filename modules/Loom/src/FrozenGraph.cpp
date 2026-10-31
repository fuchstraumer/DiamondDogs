#include "FrozenGraph.hpp"
#include "LoomDiagnostic.hpp"
#include "Records.hpp"
#include "freeze/Freeze.hpp"
#include "freeze/Publish.hpp"
#include "freeze/Resolve.hpp"
#include "freeze/Validate.hpp"
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace loom
{

std::optional<FrozenGraph> Freeze(const GraphRecord& record, DiagnosticSink& sink)
{
    const int32_t errorsBefore = sink.ErrorCount();
    freeze::FreezeContext context{ .Record = record, .Sink = sink, .Graph = FrozenGraph{}, .Keys = {} };

    context.Graph.Name = record.Name;
    context.Graph.ShaderLibraryName = record.ShaderLibraryName;

    freeze::PublishBuiltinAnchors(context);
    freeze::PublishBuffers(context);
    freeze::PublishImages(context);
    freeze::PublishSamplers(context);
    freeze::PublishImports(context);
    freeze::PublishFrameTargets(context);

    freeze::PublishAnchors(context);
    freeze::PublishSequences(context);
    // we need to collect passes to sort them into a proper ordering before freezing them
    const std::vector<freeze::PassSource> passSources = freeze::CollectPasses(record);
    freeze::PublishPasses(context, passSources);

    // check resources up to this point
    freeze::CheckDeclarations(context);

    if (sink.ErrorCount() > errorsBefore)
    {
        return std::nullopt;
    }

    return std::move(context.Graph);
}

} // namespace loom
