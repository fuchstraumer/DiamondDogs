#pragma once
#ifndef DIAMOND_DOGS_LOOM_RESOLVE_VARIANTS_HPP
#define DIAMOND_DOGS_LOOM_RESOLVE_VARIANTS_HPP
#include "VariantKey.hpp"
#include <optional>
#include <span>

// Variants.cpp: the query, the device filter, and one variant (LM203, LM204, LM205, LM207).
namespace loom
{
    struct FrozenPass;

    namespace resolve
    {
        struct MergedPin;
        struct PassShader;
        struct ResolveContext;

        [[nodiscard]] std::optional<lodestone::VariantKey> SelectVariant(ResolveContext& context,
                                                                         const FrozenPass& pass,
                                                                         const PassShader& shader,
                                                                         std::span<const MergedPin> pins);
    }
}

#endif // !DIAMOND_DOGS_LOOM_RESOLVE_VARIANTS_HPP
