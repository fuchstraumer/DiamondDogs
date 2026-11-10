#include "test_kinds.hpp"
#include "ContentDiagnostic.hpp"
#include "ExecutorTypes.hpp"
#include "Ingest.hpp"
#include "KindRegistry.hpp"
#include "ParamsBox.hpp"
#include "async/Task.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The tests never run a kind, so each Run only has to exist.
namespace cctest
{
    foundation::Task<cc::KindResult> ConstKind::Run(const Params&, cc::Inputs, cc::Context&)
    {
        co_return cc::KindResult{};
    }

    foundation::Task<cc::KindResult> ScaleKind::Run(const Params&, cc::Inputs, cc::Context&)
    {
        co_return cc::KindResult{};
    }

    foundation::Task<cc::KindResult> BlendKind::Run(const Params&, cc::Inputs, cc::Context&)
    {
        co_return cc::KindResult{};
    }

    foundation::Task<cc::KindResult> ImageKind::Run(const Params&, cc::Inputs, cc::Context&)
    {
        co_return cc::KindResult{};
    }

    foundation::Task<cc::KindResult> SharpenKind::Run(const Params&, cc::Inputs, cc::Context&)
    {
        co_return cc::KindResult{};
    }

    foundation::Task<cc::KindResult> MultiKind::Run(const Params&, cc::Inputs, cc::Context&)
    {
        co_return cc::KindResult{};
    }

    foundation::Task<cc::KindResult> MultiReorderedKind::Run(const Params&, cc::Inputs, cc::Context&)
    {
        co_return cc::KindResult{};
    }

    cc::KindRegistry MakeFrozenRegistry()
    {
        cc::KindRegistry registry;

        const bool allRegistered = registry.Register<ConstKind>().has_value() &&
                                   registry.Register<ScaleKind>().has_value() &&
                                   registry.Register<BlendKind>().has_value() &&
                                   registry.Register<ImageKind>().has_value() &&
                                   registry.Register<SharpenKind>().has_value() &&
                                   registry.Register<MultiKind>().has_value();
        assert(allRegistered);
        (void)allRegistered;

        registry.Freeze();

        return registry;
    }

    cc::KindRegistry MakeFrozenRegistryWithReorderedMulti()
    {
        cc::KindRegistry registry;

        const bool allRegistered = registry.Register<MultiReorderedKind>().has_value() &&
                                   registry.Register<SharpenKind>().has_value();
        assert(allRegistered);
        (void)allRegistered;

        registry.Freeze();

        return registry;
    }

    uint32_t AddNode(cc::AuthoredGraph& graph, std::string_view kind_name, cc::ParamsBox params)
    {
        graph.Nodes.push_back(cc::AuthoredNode{ std::string{ kind_name }, std::move(params) });

        return static_cast<uint32_t>(graph.Nodes.size()) - 1;
    }

    void Connect(cc::AuthoredGraph& graph, uint32_t from_node, std::string_view from_output, uint32_t to_node,
                 std::string_view slot)
    {
        graph.Edges.push_back(cc::AuthoredEdge{ from_node, std::string{ from_output }, to_node, std::string{ slot } });
    }

    std::vector<cc::DiagnosticCode> CodesOf(const std::vector<cc::Diagnostic>& diagnostics)
    {
        std::vector<cc::DiagnosticCode> codes;
        codes.reserve(diagnostics.size());

        for (const cc::Diagnostic& diagnostic : diagnostics)
        {
            codes.push_back(diagnostic.Code);
        }

        return codes;
    }
}
