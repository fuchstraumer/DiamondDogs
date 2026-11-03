#include "content/RecipeKey.hpp"
#include "content/HashSink.hpp"
#include "content/Key.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace
{
    std::string_view AlternativeName(const foundation::content::RecipeInput& input) noexcept;
}

namespace foundation::content
{
    namespace detail
    {
        void BeginRecipeKey(HashSink& sink, std::string_view kind, uint32_t kind_version)
        {
            sink.WriteString(k_RecipeDomain);
            sink.WriteInt(k_EncodingVersion, 4, false);
            sink.WriteString(kind);
            sink.WriteInt(kind_version, 4, false);
        }

        void AppendRecipeInputs(HashSink& sink, std::span<const RecipeInput> inputs)
        {
            sink.BeginArray(inputs.size());

            for (const RecipeInput& input : inputs)
            {
                sink.WriteString(input.Slot);
                sink.WriteVariant(AlternativeName(input));

                if (const RecipeKey* recipeKey = std::get_if<RecipeKey>(&input.Source))
                {
                    sink.WriteKeyBytes(recipeKey->Low, recipeKey->High);
                    continue;
                }

                const BlobKey& blobKey = std::get<BlobKey>(input.Source);
                sink.WriteKeyBytes(blobKey.Low, blobKey.High);
            }

            sink.EndArray();
        }

        RecipeKey FinishRecipeKey(const HashSink& sink) noexcept
        {
            const Hash128 hash = sink.Finish();

            return RecipeKey{ hash.Low, hash.High };
        }
    }

    BlobKey HashBlob(std::span<const std::byte> bytes)
    {
        HashSink sink;
        sink.WriteString(k_BlobDomain);
        sink.WriteInt(k_EncodingVersion, 4, false);
        sink.WriteInt(static_cast<int64_t>(bytes.size()), 8, false);
        sink.WriteRawBytes(bytes);

        const Hash128 hash = sink.Finish();

        return BlobKey{ hash.Low, hash.High };
    }
}

namespace
{
    std::string_view AlternativeName(const foundation::content::RecipeInput& input) noexcept
    {
        if (std::holds_alternative<foundation::content::RecipeKey>(input.Source))
        {
            return "RecipeKey";
        }

        return "BlobKey";
    }
}
