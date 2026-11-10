#pragma once
#ifndef DIAMOND_DOGS_FOUNDATION_CONTENT_RECIPE_KEY_HPP
#define DIAMOND_DOGS_FOUNDATION_CONTENT_RECIPE_KEY_HPP
#include "content/Encode.hpp"
#include "content/HashSink.hpp"
#include "content/Key.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace foundation::content
{
    // Changing any of these constants changes every key.
    inline constexpr std::string_view k_RecipeDomain = "dd.recipe";
    inline constexpr std::string_view k_BlobDomain = "dd.blob";
    inline constexpr uint32_t k_EncodingVersion = 2;

    struct RecipeInput
    {
        std::string_view Slot;
        std::variant<RecipeKey, BlobKey> Source;
        // the name of the output of the producer; a recipe source needs one, a blob source has none
        std::string_view Output;
    };

    enum class KeyErrorCode : uint8_t
    {
        Invalid = 0,
        NaNParameter,
    };

    struct KeyError
    {
        KeyErrorCode Code{ KeyErrorCode::Invalid };
        std::string FieldPath;
    };

    namespace detail
    {
        void BeginRecipeKey(CanonicalSink& sink, std::string_view kind, uint32_t kind_version);
        void AppendRecipeInputs(CanonicalSink& sink, std::span<const RecipeInput> inputs);
        RecipeKey FinishRecipeKey(const HashSink& sink) noexcept;
    }

    // Order: domain, encoding version, kind name, kind version, parameters, then the inputs.
    template<typename Params>
    std::expected<RecipeKey, KeyError> MakeRecipeKey(std::string_view kind, uint32_t kind_version,
                                                     const Params& params, std::span<const RecipeInput> inputs)
    {
        std::optional<std::string> nanPath = FindNaN(params);

        if (nanPath.has_value())
        {
            return std::unexpected(KeyError{ KeyErrorCode::NaNParameter, std::move(*nanPath) });
        }

        HashSink sink;
        detail::BeginRecipeKey(sink, kind, kind_version);
        Encode(params, sink);
        detail::AppendRecipeInputs(sink, inputs);

        return detail::FinishRecipeKey(sink);
    }

    [[nodiscard]] BlobKey HashBlob(std::span<const std::byte> bytes);
}

#endif // !DIAMOND_DOGS_FOUNDATION_CONTENT_RECIPE_KEY_HPP
