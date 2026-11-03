#pragma once
#ifndef DIAMOND_DOGS_CC_PARAMS_BOX_HPP
#define DIAMOND_DOGS_CC_PARAMS_BOX_HPP
#include "content/CanonicalSink.hpp"
#include "content/Encode.hpp"
#include "reflect/Describe.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cc
{
    namespace detail
    {
        // not const, so that the linker cannot fold the tokens of two types into one address
        template<typename P>
        inline char ParamsToken{ 0 };
    }

    // The address of this token is the identity of a params type. It needs no RTTI.
    template<typename P>
    const void* ParamsTypeIdentity() noexcept
    {
        return &detail::ParamsToken<P>;
    }

    // One table per params type. The box points to it, so the box has no virtual functions of its own.
    struct ParamsOps
    {
        const void* Identity{ nullptr };
        std::string_view TypeName;
        void* (*Clone)(const void* params){ nullptr };
        void (*Destroy)(void* params) noexcept { nullptr };
        void (*Encode)(const void* params, foundation::content::CanonicalSink& sink){ nullptr };
        std::optional<std::string> (*FindNaN)(const void* params){ nullptr };
    };

    namespace detail
    {
        template<typename P>
        void* CloneParams(const void* params)
        {
            return new P(*static_cast<const P*>(params));
        }

        template<typename P>
        void DestroyParams(void* params) noexcept
        {
            delete static_cast<P*>(params);
        }

        template<typename P>
        void EncodeParams(const void* params, foundation::content::CanonicalSink& sink)
        {
            foundation::content::Encode(*static_cast<const P*>(params), sink);
        }

        template<typename P>
        std::optional<std::string> FindNaNInParams(const void* params)
        {
            return foundation::content::FindNaN(*static_cast<const P*>(params));
        }

        template<typename P>
        inline constexpr ParamsOps k_ParamsOps
        {
            &ParamsToken<P>,
            foundation::reflect::Describe<P>::Name,
            &CloneParams<P>,
            &DestroyParams<P>,
            &EncodeParams<P>,
            &FindNaNInParams<P>,
        };
    }

    // An owning, copyable box for the params of one kind. The value lives on the heap (v0): a params struct
    // is small, and a box is copied only when a recipe is created.
    class ParamsBox
    {
    public:
        ParamsBox() noexcept = default;
        ParamsBox(const ParamsBox& _other);
        ParamsBox(ParamsBox&& _other) noexcept;
        ParamsBox& operator=(const ParamsBox& _other);
        ParamsBox& operator=(ParamsBox&& _other) noexcept;
        ~ParamsBox();

        template<foundation::reflect::DescribedStruct P>
        [[nodiscard]] static ParamsBox Make(P _value);

        [[nodiscard]] bool IsEmpty() const noexcept;

        // null when the box is empty
        [[nodiscard]] const void* TypeIdentity() const noexcept;
        [[nodiscard]] std::string_view TypeName() const noexcept;

        // null when the box is empty
        [[nodiscard]] const void* Data() const noexcept;

        // Returns the value as P, or null when the box is empty or holds another type.
        template<typename P>
        [[nodiscard]] const P* As() const noexcept;

        void Encode(foundation::content::CanonicalSink& _sink) const;
        [[nodiscard]] std::optional<std::string> FindNaN() const;

    private:
        ParamsBox(void* _data, const ParamsOps* _ops) noexcept;

        void* data{ nullptr };
        const ParamsOps* ops{ nullptr };
    };

    template<foundation::reflect::DescribedStruct P>
    ParamsBox ParamsBox::Make(P _value)
    {
        return ParamsBox{ new P(std::move(_value)), &detail::k_ParamsOps<P> };
    }

    template<typename P>
    const P* ParamsBox::As() const noexcept
    {
        if (TypeIdentity() != ParamsTypeIdentity<P>())
        {
            return nullptr;
        }

        return static_cast<const P*>(data);
    }
}

#endif // !DIAMOND_DOGS_CC_PARAMS_BOX_HPP
