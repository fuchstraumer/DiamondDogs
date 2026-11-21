#pragma once
#ifndef DIAMOND_DOGS_LOOM_BIND_SLOTS_HPP
#define DIAMOND_DOGS_LOOM_BIND_SLOTS_HPP
#include <cstdint>
#include <vector>

// Slots.cpp: which binding each Bind names (LM301), whether the key fits it (LM304), whether the narrowing only
// removes access (LM303), which bindings no Bind names (LM302), and views against their roots (LM307).
namespace loom
{
    struct FrozenPass;
    struct ResolvedPass;

    namespace bind
    {
        struct BindContext;
        struct PassLayout;

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
    }
}

#endif // !DIAMOND_DOGS_LOOM_BIND_SLOTS_HPP
