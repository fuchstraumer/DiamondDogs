#pragma once
#ifndef DIAMOND_DOGS_LOOM_RESOLVE_SHADERS_HPP
#define DIAMOND_DOGS_LOOM_RESOLVE_SHADERS_HPP
#include "resolve/ResolvedGraph.hpp"
#include <cstdint>
#include <optional>

namespace lodestone
{
    class ManifestIndex;
}

// Shaders.cpp: which module and entry points a pass names (LM201), and whether their stages fit the pass kind (LM202).
namespace loom
{
    struct FrozenPass;

    namespace resolve
    {
        struct ResolveContext;

        struct PassShader
        {
            const lodestone::ManifestIndex* Index{ nullptr };
            int32_t EntryPoint{ k_NoEntryPoint };
            int32_t FragmentEntryPoint{ k_NoEntryPoint };
        };

        [[nodiscard]] std::optional<PassShader> FindPassShader(ResolveContext& context, const FrozenPass& pass);
    }
}

#endif // !DIAMOND_DOGS_LOOM_RESOLVE_SHADERS_HPP
