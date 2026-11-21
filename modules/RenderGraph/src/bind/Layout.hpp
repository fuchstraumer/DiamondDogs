#pragma once
#ifndef DIAMOND_DOGS_LOOM_BIND_LAYOUT_HPP
#define DIAMOND_DOGS_LOOM_BIND_LAYOUT_HPP
#include "ShaderManifestLayout.hpp"
#include <optional>
#include <vector>

// Layout.cpp: the pipeline of one pass. Each resource once, with the stages that read it, in placement order, and
// the entry point constants when the pipeline has any.
namespace loom
{
    struct FrozenPass;
    struct ResolvedPass;

    namespace bind
    {
        struct BindContext;

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
    }
}

#endif // !DIAMOND_DOGS_LOOM_BIND_LAYOUT_HPP
