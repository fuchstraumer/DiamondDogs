#pragma once
#ifndef DIAMOND_DOGS_LOOM_KEYS_HPP
#define DIAMOND_DOGS_LOOM_KEYS_HPP
#include <cstdint>
#include <bit>
#include <string_view>

namespace loom
{

    namespace detail
    {
        // Compact ARX (Add-Rotate-XOR) consteval string hash.
        //
        // Architecture and constants derived from:
        // 1. SipHash-2-4 by Jean-Philippe Aumasson and Daniel J. Bernstein (2012)
        //    "SipHash: a fast short-input PRF" (https://131002.net/siphash/)
        // 2. HalfSipHash / 2-word ARX round structure inspired by Skein/Threefish
        //    by Ferguson, Lucks, Schneier, et al. (2010)
        consteval uint64_t HashArxSv(std::string_view str) noexcept
        {
            constexpr uint64_t k_Prime = 0x736f6d6570736575ULL;
            constexpr uint64_t k_Prime2 = 0x646f72616e646f6dULL;
            uint64_t v0 = k_Prime;
            uint64_t v1 = k_Prime2 ^ str.size();

            auto mix = [&v0, &v1]() noexcept
            {
                v0 += v1;
                v1 = std::rotl(v1, 13) ^ v0;
                v0 = std::rotl(v0, 32);
                v0 += v1;
                v1 = std::rotl(v1, 17) ^ v0;
                v0 = std::rotl(v0, 32);
            };

            for (unsigned char c : str)
            {
                v1 ^= static_cast<uint64_t>(c);
                mix();
                v0 ^= static_cast<uint64_t>(c);
            }

            v1 ^= 0xFF;
            mix();
            mix();
            mix();

            return v0 ^ v1;
        }

        // Key strings must follow this convention, otherwise scoping doesn't work.
        constexpr bool IsModuleDotName(std::string_view str) noexcept
        {
            // name needs to be non-empty on both sides of `.`, and contain `.` somewhere in the middle
            auto dotPos = str.find('.');
            return !str.empty() &&
                    dotPos != std::string_view::npos &&
                    dotPos != 0 &&
                    dotPos != str.size() - 1;
        }

        // consteval call to undefined function is a compile-time error, with the name
        // of the function, so this works as a nice static assertion mechanism for key naming conventions.
        void LoomKeyMustBeModuleDotName();
    }

    // type-erased wrapper to just hold the data for any key type - used for diagnostic
    // logs and errors
    struct KeyRef
    {
        std::string_view Name;
        uint64_t Hash{ 0u };
    };

    // tag types for disambiguating keys on type, so we can check at compile-time
    // that the correct key type is being used where keys are required
    struct Buffer;
    struct Image;
    struct Sampler;

    template<typename Kind>
    struct ResourceKey
    {
        consteval ResourceKey(std::string_view name)
            : Name(name), Hash(detail::HashArxSv(name))
        {
            if (!detail::IsModuleDotName(name))
            {
                detail::LoomKeyMustBeModuleDotName();
            }
        }

        [[nodiscard]] constexpr KeyRef ToRef() const noexcept
        {
            return KeyRef{ Name, Hash };
        }

        constexpr bool operator==(const ResourceKey& other) const noexcept
        {
            return Hash == other.Hash;
        }

        constexpr bool operator!=(const ResourceKey& other) const noexcept
        {
            return !(*this == other);
        }


        std::string_view Name;
        uint64_t Hash{ 0u };
    };

    template<typename T>
    struct BaseKey
    {
        consteval BaseKey(std::string_view name)
            : Name(name), Hash(detail::HashArxSv(name))
        {
            if (!detail::IsModuleDotName(name))
            {
                detail::LoomKeyMustBeModuleDotName();
            }
        }

        [[nodiscard]] constexpr KeyRef ToRef() const noexcept
        {
            return KeyRef{ Name, Hash };
        }

        constexpr bool operator==(const BaseKey& other) const noexcept
        {
            return Hash == other.Hash;
        }

        constexpr bool operator!=(const BaseKey& other) const noexcept
        {
            return !(*this == other);
        }

        std::string_view Name;
        uint64_t Hash{ 0u };
    };

    using AnchorKey = BaseKey<struct AnchorTag>;
    using PassKey = BaseKey<struct PassTag>;
    using SequenceKey = BaseKey<struct SequenceTag>;
    using FrameTargetKey = BaseKey<struct FrameTargetTag>;

    template<typename T>
    struct ParamsKey
    {
        consteval ParamsKey(std::string_view name)
            : Name(name), Hash(detail::HashArxSv(name))
        {
            if (!detail::IsModuleDotName(name))
            {
                detail::LoomKeyMustBeModuleDotName();
            }
        }

        [[nodiscard]] constexpr KeyRef ToRef() const noexcept
        {
            return KeyRef{ Name, Hash };
        }

        constexpr bool operator==(const ParamsKey& other) const noexcept
        {
            return Hash == other.Hash;
        }

        constexpr bool operator!=(const ParamsKey& other) const noexcept
        {
            return !(*this == other);
        }

        std::string_view Name;
        uint64_t Hash{ 0u };
    };

    inline constexpr AnchorKey BeginFrameAnchor("Loom.BeginFrame");
    inline constexpr AnchorKey EndFrameAnchor("Loom.EndFrame");

}

#endif // !DIAMOND_DOGS_LOOM_KEYS_HPP
