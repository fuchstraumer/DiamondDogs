#pragma once
#ifndef DIAMOND_DOGS_CC_KIND_REGISTRY_HPP
#define DIAMOND_DOGS_CC_KIND_REGISTRY_HPP
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "ExecutorTypes.hpp"
#include "ParamsBox.hpp"
#include "async/Task.hpp"
#include "reflect/Describe.hpp"
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace cc
{
    namespace detail
    {
        // constexpr, so that the Kind concept can check the slots of a kind at compile time
        constexpr bool SlotsAreWellFormed(std::span<const Slot> slots) noexcept
        {
            for (size_t index = 0; index < slots.size(); ++index)
            {
                const Slot& slot = slots[index];

                const bool isIncomplete = slot.Name.empty() || slot.Use == SlotUse::Invalid ||
                                          slot.Accepts == ArtifactKind::Invalid;

                if (isIncomplete)
                {
                    return false;
                }

                for (size_t earlier = 0; earlier < index; ++earlier)
                {
                    if (slots[earlier].Name == slot.Name)
                    {
                        return false;
                    }
                }
            }

            return true;
        }
    }

    // A kind is a struct with a name, a version, a params type, its slots, its output and a Run function.
    template<typename K>
    concept Kind = requires
    {
        { K::Name } -> std::convertible_to<std::string_view>;
        requires (!std::string_view{ K::Name }.empty());
        { K::Version } -> std::convertible_to<uint32_t>;
        typename K::Params;
        requires foundation::reflect::DescribedStruct<typename K::Params>;
        requires detail::SlotsAreWellFormed(std::span<const Slot>{ K::Slots });
        { K::Output } -> std::convertible_to<ArtifactKind>;
        requires (K::Output != ArtifactKind::Invalid);
        {
            K::Run(std::declval<const typename K::Params&>(), std::declval<Inputs>(),
                   std::declval<Context&>())
        } -> std::same_as<foundation::Task<Result>>;
    };

    // Run takes the params as a pointer to the kind's Params type, which the thunk of Register<K> casts back.
    using KindRunFunction = foundation::Task<Result>(*)(const void* params, Inputs inputs, Context& context);

    struct KindOps
    {
        std::string_view Name;
        uint32_t Version{ 0 };
        // points into the kind's static array
        std::span<const Slot> Slots;
        ArtifactKind Output{ ArtifactKind::Invalid };
        const void* ParamsType{ nullptr };
        std::string_view ParamsTypeName;
        KindRunFunction Run{ nullptr };
    };

    namespace detail
    {
        template<typename K>
        foundation::Task<Result> RunKind(const void* params, Inputs inputs, Context& context)
        {
            const typename K::Params& typedParams = *static_cast<const typename K::Params*>(params);

            return K::Run(typedParams, std::move(inputs), context);
        }

        template<typename K>
        KindOps MakeKindOps()
        {
            return KindOps
            {
                .Name = K::Name,
                .Version = K::Version,
                .Slots = std::span<const Slot>{ K::Slots },
                .Output = K::Output,
                .ParamsType = ParamsTypeIdentity<typename K::Params>(),
                .ParamsTypeName = foundation::reflect::Describe<typename K::Params>::Name,
                .Run = &RunKind<K>,
            };
        }
    }

    class KindRegistry
    {
    public:
        // Returns DuplicateKindName for a name that is registered already. Asserts after Freeze.
        template<Kind K>
        std::expected<KindId, Diagnostic> Register();

        void Freeze() noexcept;
        [[nodiscard]] bool IsFrozen() const noexcept;

        [[nodiscard]] std::optional<KindId> Find(std::string_view _name) const;

        // The reference stays valid until the next Register.
        [[nodiscard]] const KindOps& Ops(KindId _id) const;

        [[nodiscard]] size_t Count() const noexcept;

    private:
        std::expected<KindId, Diagnostic> addKind(KindOps _ops);

        std::vector<KindOps> kinds;
        bool frozen{ false };
    };

    template<Kind K>
    std::expected<KindId, Diagnostic> KindRegistry::Register()
    {
        return addKind(detail::MakeKindOps<K>());
    }
}

#endif // !DIAMOND_DOGS_CC_KIND_REGISTRY_HPP
