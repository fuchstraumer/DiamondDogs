#include "RecipeTable.hpp"
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "KindRegistry.hpp"
#include "ParamsBox.hpp"
#include "RecipeParams.hpp"
#include "content/BytesSink.hpp"
#include "content/CanonicalSink.hpp"
#include "content/Encode.hpp"
#include "content/HashSink.hpp"
#include "content/Key.hpp"
#include "content/RecipeKey.hpp"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void WriteRecipeParams(foundation::content::CanonicalSink& sink, cc::Quality level,
                           const cc::ParamsBox& params);
    std::vector<std::byte> WriteCanonicalBytes(const cc::KindOps& kind, cc::Quality level,
                                               const cc::ParamsBox& params,
                                               std::span<const foundation::content::RecipeInput> inputs);
    foundation::content::RecipeKey HashCanonicalBytes(std::span<const std::byte> bytes);
}

namespace cc
{
    const Recipe& RecipeTable::Get(RecipeId _id) const
    {
        assert(_id.Value >= 1 && _id.Value <= stored.size() && "the id was not made by this table");

        return stored[_id.Value - 1].Value;
    }

    std::optional<RecipeId> RecipeTable::Find(const foundation::content::RecipeKey& _key) const
    {
        const auto found = idsByKey.find(_key);

        if (found == idsByKey.end())
        {
            return std::nullopt;
        }

        return found->second;
    }

    size_t RecipeTable::Count() const noexcept
    {
        return stored.size();
    }

    std::span<const std::byte> RecipeTable::CanonicalBytes(RecipeId _id) const
    {
        assert(_id.Value >= 1 && _id.Value <= stored.size() && "the id was not made by this table");

        return stored[_id.Value - 1].Bytes;
    }

    PreparedRecipe RecipeTable::Prepare(const KindRegistry& _registry, KindId _kind, Quality _level,
                                        const ParamsBox& _params,
                                        std::span<const foundation::content::RecipeInput> _inputs)
    {
        const KindOps& kind = _registry.Ops(_kind);
        assert(_params.TypeIdentity() == kind.ParamsType && "the params do not belong to this kind");

        std::vector<std::byte> bytes = WriteCanonicalBytes(kind, _level, _params, _inputs);
        const foundation::content::RecipeKey key = HashCanonicalBytes(bytes);

        return PreparedRecipe{ key, std::move(bytes) };
    }

    std::expected<RecipeId, Diagnostic> RecipeTable::Intern(const KindRegistry& _registry, KindId _kind,
                                                            Quality _level, const ParamsBox& _params,
                                                            std::span<const RecipeInputRef> _inputs)
    {
        std::vector<foundation::content::RecipeInput> keyInputs;
        keyInputs.reserve(_inputs.size());

        for (const RecipeInputRef& input : _inputs)
        {
            const Recipe& source = Get(input.Source);
            keyInputs.push_back(foundation::content::RecipeInput{ input.Slot, source.Key });
        }

        PreparedRecipe prepared = Prepare(_registry, _kind, _level, _params, keyInputs);

        const std::expected<std::optional<RecipeId>, Diagnostic> existing = Lookup(prepared.Key, prepared.Bytes);

        if (!existing.has_value())
        {
            return std::unexpected(existing.error());
        }

        if (existing->has_value())
        {
            return **existing;
        }

        Recipe recipe
        {
            .Key = prepared.Key,
            .Kind = _kind,
            .Level = _level,
            .Params = _params,
            .Inputs = std::vector<RecipeInputRef>(_inputs.begin(), _inputs.end()),
        };

        return append(std::move(recipe), std::move(prepared.Bytes));
    }

    std::expected<RecipeId, Diagnostic> RecipeTable::InternComputed(Recipe _recipe,
                                                                    std::vector<std::byte> _bytes)
    {
        const std::expected<std::optional<RecipeId>, Diagnostic> existing = Lookup(_recipe.Key, _bytes);

        if (!existing.has_value())
        {
            return std::unexpected(existing.error());
        }

        if (existing->has_value())
        {
            return **existing;
        }

        return append(std::move(_recipe), std::move(_bytes));
    }

    std::expected<std::optional<RecipeId>, Diagnostic>
    RecipeTable::Lookup(const foundation::content::RecipeKey& _key, std::span<const std::byte> _bytes) const
    {
        const std::optional<RecipeId> found = Find(_key);

        if (!found.has_value())
        {
            return std::optional<RecipeId>{};
        }

        const std::span<const std::byte> storedBytes = CanonicalBytes(*found);

        if (std::ranges::equal(storedBytes, _bytes))
        {
            return found;
        }

        const std::string keyText = foundation::content::ToHex(_key);
        const std::string message = std::format("recipe key {} is stored with other canonical bytes", keyText);

        return std::unexpected(Diagnostic
        {
            .Level = Severity::Error,
            .Code = DiagnosticCode::KeyCollision,
            .Message = message,
        });
    }

    RecipeId RecipeTable::append(Recipe _recipe, std::vector<std::byte> _bytes)
    {
        const RecipeId id{ static_cast<uint32_t>(stored.size()) + 1 };
        const foundation::content::RecipeKey key = _recipe.Key;

        stored.push_back(StoredRecipe{ std::move(_recipe), std::move(_bytes) });
        idsByKey.emplace(key, id);

        return id;
    }
}

namespace
{
    // The same stream as Encode makes for cc::RecipeParams<P>, written by hand because the params are
    // type-erased.
    void WriteRecipeParams(foundation::content::CanonicalSink& sink, cc::Quality level,
                           const cc::ParamsBox& params)
    {
        sink.BeginStruct(cc::k_RecipeParamsName);
        sink.BeginField("Quality");
        foundation::content::Encode(level, sink);
        sink.BeginField("Params");
        params.Encode(sink);
        sink.EndStruct();
    }

    std::vector<std::byte> WriteCanonicalBytes(const cc::KindOps& kind, cc::Quality level,
                                               const cc::ParamsBox& params,
                                               std::span<const foundation::content::RecipeInput> inputs)
    {
        foundation::content::BytesSink sink;
        foundation::content::detail::BeginRecipeKey(sink, kind.Name, kind.Version);
        WriteRecipeParams(sink, level, params);
        foundation::content::detail::AppendRecipeInputs(sink, inputs);

        return sink.TakeBytes();
    }

    foundation::content::RecipeKey HashCanonicalBytes(std::span<const std::byte> bytes)
    {
        foundation::content::HashSink sink;
        sink.WriteRawBytes(bytes);

        return foundation::content::detail::FinishRecipeKey(sink);
    }
}
