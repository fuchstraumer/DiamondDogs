#pragma once
#ifndef DIAMOND_DOGS_LOOM_BIND_CONTEXT_HPP
#define DIAMOND_DOGS_LOOM_BIND_CONTEXT_HPP
#include "bind/BoundGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "Records.hpp"
#include "ShaderManifestLayout.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The Bind stage's internals, shared by its source files and by the tests. The public part (BoundGraph, Bind) is in
// BoundGraph.hpp. Each step lives in the file named for the question it answers.
namespace loom
{
    class DiagnosticSink;

    namespace bind
    {
        struct BindContext
        {
            const FrozenGraph& Frozen;
            DiagnosticSink& Sink;
        };

        // Layout.cpp: the pipeline of one pass. Each resource once, with the stages that read it, in placement order,
        // and the entry point constants when the pipeline has any.

        struct PassLayout
        {
            std::vector<lodestone::manifest::MergedResource> Resources;
            std::optional<lodestone::manifest::EntryPointConstantsView> Constants;
        };

        // The entry point instances of a resolved pass: the compute one, or the vertex one and the fragment one.
        [[nodiscard]] std::vector<lodestone::manifest::EntryPointInstanceView> PassInstances(
            const ResolvedPass& resolved);

        // Two stages that disagree at one placement cannot share a pipeline layout: LM304 on the first such binding.
        [[nodiscard]] std::optional<PassLayout> MergePassLayout(BindContext& context,
                                                                const FrozenPass& pass,
                                                                const ResolvedPass& resolved);

        // Slots.cpp: which binding each Bind names (LM301), whether the key fits it (LM304), whether the narrowing
        // only removes access (LM303), and which bindings no Bind names (LM302).

        inline constexpr int32_t k_NotInVariant = -1;

        // [bind] the position of its binding in layout.Resources, or k_NotInVariant when the name is only in other
        // variants of the entry points. A name in no variant at all is LM301, and also k_NotInVariant.
        [[nodiscard]] std::vector<int32_t> MatchBinds(BindContext& context,
                                                      const FrozenPass& pass,
                                                      const ResolvedPass& resolved,
                                                      const PassLayout& layout);

        void CheckUnboundResources(BindContext& context,
                                   const FrozenPass& pass,
                                   const PassLayout& layout,
                                   const std::vector<int32_t>& matches);

        // LM307: in one pass, a view and its root name one memory, so their binds must name one key.
        void CheckViews(BindContext& context,
                        const FrozenPass& pass,
                        const PassLayout& layout,
                        const std::vector<int32_t>& matches);

        // Params.cpp: whether the Params statement fits the entry point constants (LM305), and whether constants
        // that hold caller data have a Params statement (LM302).
        [[nodiscard]] BoundParams BindPassParams(BindContext& context, const FrozenPass& pass, const PassLayout& layout);

        // Helpers.cpp

        // The name that Bind("name", key) matches. Stub for Lodestone request C26: a shim in an entry point parameter
        // reflects as the shim's inner field ("data") in the scope "entryPointParams_<parameter>", so Loom takes the
        // parameter name from the scope.
        [[nodiscard]] std::string_view BindingNameOf(const lodestone::manifest::ResolvedResource& resource);

        // The binding that `resource` is a view of, or empty when it is a root. A view is another typed binding of
        // the same memory: an atomic buffer and a plain read-only binding of it, for example. Stub for Lodestone
        // request C27: the shader states it as the tag "ViewOf:<root binding name>".
        [[nodiscard]] std::string_view ViewRootOf(const lodestone::manifest::ResolvedResource& resource);

        // Lodestone's reflected access as Order's access. A uniform buffer and a sampler reflect no access: both are
        // reads. Append, consume and rasterizer-ordered access read and write.
        [[nodiscard]] ResourceAccess ToResourceAccess(lodestone::ResourceAccess access);

        // The access that a narrowing leaves. No narrowing keeps the reflected access.
        [[nodiscard]] ResourceAccess NarrowedAccess(ResourceAccess reflected, BindNarrowing narrowing);

        // True when a key of `kind` can fill a binding of `binding_kind`.
        [[nodiscard]] bool KeyFitsBinding(ResourceKind kind, lodestone::BindingKind binding_kind);

    }
}

#endif // !DIAMOND_DOGS_LOOM_BIND_CONTEXT_HPP
