#pragma once
#ifndef DIAMOND_DOGS_CC_RECIPE_TABLE_HPP
#define DIAMOND_DOGS_CC_RECIPE_TABLE_HPP
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "KindRegistry.hpp"
#include "ParamsBox.hpp"
#include "content/Key.hpp"
#include "content/RecipeKey.hpp"
#include <cstddef>
#include <deque>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cc
{
    struct RecipeInputRef
    {
        // points into the static slot array of the consumer's kind
        std::string_view Slot;
        RecipeId Source;
    };

    struct Recipe
    {
        foundation::content::RecipeKey Key;
        KindId Kind;
        Quality Level{ Quality::Invalid };
        ParamsBox Params;
        // in slot order; an optional slot with no edge is absent
        std::vector<RecipeInputRef> Inputs;
    };

    // The canonical bytes of a recipe and the key that hashes them. Nothing is stored yet.
    struct PreparedRecipe
    {
        foundation::content::RecipeKey Key;
        std::vector<std::byte> Bytes;
    };

    // The interner. The table lives across ingestions and only grows. Ids start at 1. Equal keys are not
    // enough to share an id: the canonical bytes must be equal too (a collision is reported, never merged).
    class RecipeTable
    {
    public:
        // The reference stays valid for the life of the table.
        [[nodiscard]] const Recipe& Get(RecipeId _id) const;
        [[nodiscard]] std::optional<RecipeId> Find(const foundation::content::RecipeKey& _key) const;
        [[nodiscard]] size_t Count() const noexcept;

        // the bytes that were hashed to make the key of this recipe
        [[nodiscard]] std::span<const std::byte> CanonicalBytes(RecipeId _id) const;

        // Writes the canonical stream of a recipe and hashes it. The key depends on the keys of the inputs and
        // not on their ids, so a caller can prepare a whole graph before the table holds any of it.
        [[nodiscard]] static PreparedRecipe Prepare(const KindRegistry& _registry, KindId _kind, Quality _level,
                                                    const ParamsBox& _params,
                                                    std::span<const foundation::content::RecipeInput> _inputs);

        // Returns the id of the recipe with this key and these bytes, nothing for a new key, or KeyCollision for
        // a stored key with other bytes. Changes nothing.
        [[nodiscard]] std::expected<std::optional<RecipeId>, Diagnostic>
        Lookup(const foundation::content::RecipeKey& _key, std::span<const std::byte> _bytes) const;

        // Prepares the recipe, looks it up, and adds it when it is new. The sources of the inputs must be in this
        // table already.
        std::expected<RecipeId, Diagnostic> Intern(const KindRegistry& _registry, KindId _kind,
                                                   Quality _level, const ParamsBox& _params,
                                                   std::span<const RecipeInputRef> _inputs);

        // The step under Intern, for a caller that already has the key and the bytes. A test uses it to
        // force two different byte streams under one key.
        std::expected<RecipeId, Diagnostic> InternComputed(Recipe _recipe, std::vector<std::byte> _bytes);

    private:
        struct StoredRecipe
        {
            Recipe Value;
            std::vector<std::byte> Bytes;
        };

        RecipeId append(Recipe _recipe, std::vector<std::byte> _bytes);

        // a deque, so that a reference from Get survives later growth
        std::deque<StoredRecipe> stored;
        std::unordered_map<foundation::content::RecipeKey, RecipeId,
                           foundation::content::KeyHasher<foundation::content::RecipeTag>> idsByKey;
    };
}

#endif // !DIAMOND_DOGS_CC_RECIPE_TABLE_HPP
