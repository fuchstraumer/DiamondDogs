#include "resolve/ResolvedGraph.hpp"
#include "resolve/ResolveContext.hpp"
#include "LoomDiagnostic.hpp"
#include "freeze/FrozenGraph.hpp"
#include <cstdint>
#include <optional>
#include <vector>

// The Resolve stage's steps, in the order they run. Each step lives in the file named for the question it answers.
namespace loom
{

    std::optional<ResolvedGraph> Resolve(const FrozenGraph& frozen,
                                         const ResolveInputs& inputs,
                                         DiagnosticSink& sink)
    {
        using namespace resolve;
        ResolveContext context
        {
            .Frozen = frozen,
            .Inputs = inputs,
            .Sink = sink,
            .GraphPinDeclared = std::vector<uint8_t>(frozen.Pins.size(), uint8_t{ 0 }),
        };

        const int32_t errorsBefore = sink.ErrorCount();
        ResolvedGraph result;
        result.Passes.resize(frozen.Passes.size());

        for (size_t passIndex = 0; passIndex < frozen.Passes.size(); ++passIndex)
        {
            const FrozenPass& pass = frozen.Passes[passIndex];

            // a Record pass runs a callback, not a shader
            if (pass.Kind == PassKind::Record)
            {
                continue;
            }

            const std::optional<PassShader> shader = FindPassShader(context, pass);

            if (!shader.has_value())
            {
                continue;
            }

            const std::vector<MergedPin> pins = MergePins(context, pass, *shader->Index);
            const std::optional<lodestone::VariantKey> variant = SelectVariant(context, pass, *shader, pins);

            if (!variant.has_value())
            {
                continue;
            }

            ResolvedPass& resolved = result.Passes[passIndex];
            resolved.Environment = &shader->Index->View();
            resolved.Variant = *variant;
            resolved.EntryPoint = shader->EntryPoint;
            resolved.FragmentEntryPoint = shader->FragmentEntryPoint;
        }

        if (sink.ErrorCount() != errorsBefore)
        {
            return std::nullopt;
        }

        // A graph pin that only a failed pass's module declares would look undeclared, so LM206 waits for a clean graph
        CheckGraphPinsDeclared(context);
        return result;
    }

}
