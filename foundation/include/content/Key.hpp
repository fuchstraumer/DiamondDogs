#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_KEY_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_KEY_HPP

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace foundation::content
{
    struct RecipeTag;
    struct BlobTag;
    struct ArtifactTag;

    template<typename Tag>
    struct Key
    {
        uint64_t Low{ 0 };
        uint64_t High{ 0 };

        friend constexpr bool operator==(const Key&, const Key&) noexcept = default;
        friend constexpr auto operator<=>(const Key&, const Key&) noexcept = default;
    };

    using RecipeKey = Key<RecipeTag>;
    using BlobKey = Key<BlobTag>;
    using ArtifactHash = Key<ArtifactTag>;

    // for std::unordered_map. Low is already well mixed by XXH3
    template<typename Tag>
    struct KeyHasher
    {
        size_t operator()(const Key<Tag>& key) const noexcept
        {
            return static_cast<size_t>(key.Low);
        }
    };

    // the hex text is High first, then Low, lowercase
    void WriteHexWords(uint64_t high, uint64_t low, std::span<char, 32> out) noexcept;

    template<typename Tag>
    void WriteHex(const Key<Tag>& key, std::span<char, 32> out) noexcept
    {
        WriteHexWords(key.High, key.Low, out);
    }

    template<typename Tag>
    std::string ToHex(const Key<Tag>& key)
    {
        std::string text(32, '\0');
        WriteHexWords(key.High, key.Low, std::span<char, 32>{ text.data(), 32 });
        return text;
    }
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_KEY_HPP
