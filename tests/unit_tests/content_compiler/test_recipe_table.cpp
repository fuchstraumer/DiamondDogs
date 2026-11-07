#include <gtest/gtest.h>

#include "test_kinds.hpp"
#include "ContentDiagnostic.hpp"
#include "ContentTypes.hpp"
#include "KindRegistry.hpp"
#include "RecipeParams.hpp"
#include "RecipeTable.hpp"
#include "content/HashSink.hpp"
#include "content/Key.hpp"
#include "content/RecipeKey.hpp"

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    // Makes the key through foundation's typed path, which shares no code with the type-erased path of the table.
    template<typename P>
    foundation::content::RecipeKey ReferenceKey(std::string_view kind, uint32_t version, cc::Quality quality,
                                                const P& params,
                                                std::span<const foundation::content::RecipeInput> inputs);

    cc::RecipeId InternOrFail(cc::RecipeTable& table, const cc::KindRegistry& registry, std::string_view kind_name,
                              cc::Quality quality, const cc::ParamsBox& params,
                              std::span<const cc::RecipeInputRef> inputs);
}

TEST(RecipeTableTest, KeyOfAKindWithNoInputsEqualsTheTypedKey)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    cctest::ConstParams params;
    params.Value = 0.75f;
    params.Seed = 12;
    params.Mode = cctest::Shape::Ridged;
    params.Nested.Weights = { 1.0f, 2.0f };

    const cc::RecipeId id =
        InternOrFail(table, registry, "test.Const", cc::Quality::Preview, cc::ParamsBox::Make(params), {});

    const foundation::content::RecipeKey expected = ReferenceKey("test.Const", 1, cc::Quality::Preview, params, {});

    EXPECT_EQ(table.Get(id).Key, expected);
    EXPECT_EQ(table.Find(expected), id);
}

TEST(RecipeTableTest, KeyOfAKindWithOneInputEqualsTheTypedKey)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cctest::ConstParams sourceParams{ 3.0f, 5, cctest::Shape::Flat, {} };
    const cctest::ScaleParams scaleParams{ 2.0f };

    const cc::RecipeId source =
        InternOrFail(table, registry, "test.Const", cc::Quality::Final, cc::ParamsBox::Make(sourceParams), {});

    const std::array<cc::RecipeInputRef, 1> inputs{ cc::RecipeInputRef{ "Source", source, 0 } };
    const cc::RecipeId scale =
        InternOrFail(table, registry, "test.Scale", cc::Quality::Final, cc::ParamsBox::Make(scaleParams), inputs);

    const foundation::content::RecipeKey sourceKey =
        ReferenceKey("test.Const", 1, cc::Quality::Final, sourceParams, {});

    const std::array<foundation::content::RecipeInput, 1> referenceInputs
    {
        foundation::content::RecipeInput{ "Source", sourceKey, "Value" },
    };

    EXPECT_EQ(table.Get(scale).Key, ReferenceKey("test.Scale", 3, cc::Quality::Final, scaleParams, referenceInputs));
}

TEST(RecipeTableTest, KeyOfAKindWithTwoInputsAndAnAbsentOptionalSlotEqualsTheTypedKey)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cctest::ConstParams firstParams{ 1.0f, 1, cctest::Shape::Flat, {} };
    const cctest::ConstParams secondParams{ 2.0f, 2, cctest::Shape::Ridged, {} };
    const cctest::BlendParams blendParams{ 0.25f, true };

    const cc::RecipeId first =
        InternOrFail(table, registry, "test.Const", cc::Quality::Preview, cc::ParamsBox::Make(firstParams), {});
    const cc::RecipeId second =
        InternOrFail(table, registry, "test.Const", cc::Quality::Preview, cc::ParamsBox::Make(secondParams), {});

    // Mask is optional and has no edge, so it is not in the list
    const std::array<cc::RecipeInputRef, 2> inputs
    {
        cc::RecipeInputRef{ "A", first, 0 },
        cc::RecipeInputRef{ "B", second, 0 },
    };

    const cc::RecipeId blend =
        InternOrFail(table, registry, "test.Blend", cc::Quality::Preview, cc::ParamsBox::Make(blendParams), inputs);

    const foundation::content::RecipeKey firstKey =
        ReferenceKey("test.Const", 1, cc::Quality::Preview, firstParams, {});
    const foundation::content::RecipeKey secondKey =
        ReferenceKey("test.Const", 1, cc::Quality::Preview, secondParams, {});

    const std::array<foundation::content::RecipeInput, 2> referenceInputs
    {
        foundation::content::RecipeInput{ "A", firstKey, "Value" },
        foundation::content::RecipeInput{ "B", secondKey, "Value" },
    };

    EXPECT_EQ(table.Get(blend).Key,
              ReferenceKey("test.Blend", 2, cc::Quality::Preview, blendParams, referenceInputs));
}

TEST(RecipeTableTest, KeyOfARecipeThatReadsANonFirstOutputEqualsTheTypedKey)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cctest::ImageParams multiParams{ 64 };
    const cctest::ScaleParams sharpenParams{ 1.5f };

    const cc::RecipeId multi =
        InternOrFail(table, registry, "test.Multi", cc::Quality::Final, cc::ParamsBox::Make(multiParams), {});

    // Normal is the second output of test.Multi
    const std::array<cc::RecipeInputRef, 1> inputs{ cc::RecipeInputRef{ "Source", multi, 1 } };
    const cc::RecipeId sharpen =
        InternOrFail(table, registry, "test.Sharpen", cc::Quality::Final, cc::ParamsBox::Make(sharpenParams), inputs);

    const foundation::content::RecipeKey multiKey =
        ReferenceKey("test.Multi", 1, cc::Quality::Final, multiParams, {});

    const std::array<foundation::content::RecipeInput, 1> referenceInputs
    {
        foundation::content::RecipeInput{ "Source", multiKey, "Normal" },
    };

    EXPECT_EQ(table.Get(sharpen).Key,
              ReferenceKey("test.Sharpen", 1, cc::Quality::Final, sharpenParams, referenceInputs));
    EXPECT_EQ(table.Get(sharpen).Inputs[0].Output, 1);
}

TEST(RecipeTableTest, TwoRecipesThatReadDifferentOutputsOfOneProducerGetDifferentKeysAndIds)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::RecipeId multi =
        InternOrFail(table, registry, "test.Multi", cc::Quality::Final, cc::ParamsBox::Make(cctest::ImageParams{ 64 }), {});
    const cc::ParamsBox sharpenBox = cc::ParamsBox::Make(cctest::ScaleParams{ 1.5f });

    const std::array<cc::RecipeInputRef, 1> readsHeight{ cc::RecipeInputRef{ "Source", multi, 0 } };
    const std::array<cc::RecipeInputRef, 1> readsNormal{ cc::RecipeInputRef{ "Source", multi, 1 } };

    const cc::RecipeId height = InternOrFail(table, registry, "test.Sharpen", cc::Quality::Final, sharpenBox, readsHeight);
    const cc::RecipeId normal = InternOrFail(table, registry, "test.Sharpen", cc::Quality::Final, sharpenBox, readsNormal);

    EXPECT_NE(height, normal);
    EXPECT_NE(table.Get(height).Key, table.Get(normal).Key);
}

TEST(RecipeTableTest, QualityChangesTheKey)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::ParamsBox box = cc::ParamsBox::Make(cctest::ConstParams{});

    const cc::RecipeId preview = InternOrFail(table, registry, "test.Const", cc::Quality::Preview, box, {});
    const cc::RecipeId finalId = InternOrFail(table, registry, "test.Const", cc::Quality::Final, box, {});

    EXPECT_NE(preview, finalId);
    EXPECT_NE(table.Get(preview).Key, table.Get(finalId).Key);
    EXPECT_EQ(table.Get(finalId).Key, ReferenceKey("test.Const", 1, cc::Quality::Final, cctest::ConstParams{}, {}));
}

TEST(RecipeTableTest, IdsStartAtOneAndAnEqualRecipeKeepsItsId)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;
    const cc::ParamsBox box = cc::ParamsBox::Make(cctest::ConstParams{});

    const cc::RecipeId first = InternOrFail(table, registry, "test.Const", cc::Quality::Final, box, {});
    const cc::RecipeId again = InternOrFail(table, registry, "test.Const", cc::Quality::Final, box, {});

    EXPECT_EQ(first.Value, 1u);
    EXPECT_EQ(again, first);
    EXPECT_EQ(table.Count(), 1u);
}

TEST(RecipeTableTest, ARecipeStoresItsKindLevelParamsAndInputsInSlotOrder)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::RecipeId source =
        InternOrFail(table, registry, "test.Const", cc::Quality::Final, cc::ParamsBox::Make(cctest::ConstParams{}), {});

    const std::array<cc::RecipeInputRef, 1> inputs{ cc::RecipeInputRef{ "Source", source, 0 } };
    const cc::RecipeId scale =
        InternOrFail(table, registry, "test.Scale", cc::Quality::Final,
                     cc::ParamsBox::Make(cctest::ScaleParams{ 6.0f }), inputs);

    const cc::Recipe& recipe = table.Get(scale);

    EXPECT_EQ(recipe.Kind, *registry.Find("test.Scale"));
    EXPECT_EQ(recipe.Level, cc::Quality::Final);
    ASSERT_NE(recipe.Params.As<cctest::ScaleParams>(), nullptr);
    EXPECT_EQ(recipe.Params.As<cctest::ScaleParams>()->Factor, 6.0f);
    ASSERT_EQ(recipe.Inputs.size(), 1u);
    EXPECT_EQ(recipe.Inputs[0].Slot, "Source");
    EXPECT_EQ(recipe.Inputs[0].Source, source);
}

TEST(RecipeTableTest, ReferencesFromGetSurviveLaterGrowth)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::RecipeId first =
        InternOrFail(table, registry, "test.Const", cc::Quality::Final, cc::ParamsBox::Make(cctest::ConstParams{}), {});
    const cc::Recipe& firstRecipe = table.Get(first);
    const foundation::content::RecipeKey firstKey = firstRecipe.Key;

    for (uint32_t seed = 1; seed <= 200; ++seed)
    {
        cctest::ConstParams params;
        params.Seed = seed;
        InternOrFail(table, registry, "test.Const", cc::Quality::Final, cc::ParamsBox::Make(params), {});
    }

    EXPECT_EQ(table.Count(), 201u);
    EXPECT_EQ(firstRecipe.Key, firstKey);
}

TEST(RecipeTableTest, EqualBytesUnderOneKeyShareAnId)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::ParamsBox box = cc::ParamsBox::Make(cctest::ConstParams{});
    const cc::RecipeId id = InternOrFail(table, registry, "test.Const", cc::Quality::Final, box, {});

    const cc::Recipe& stored = table.Get(id);
    const std::span<const std::byte> storedBytes = table.CanonicalBytes(id);

    cc::Recipe sameKey{ .Key = stored.Key, .Kind = stored.Kind, .Level = stored.Level, .Params = box, .Inputs = {} };
    const std::vector<std::byte> sameBytes(storedBytes.begin(), storedBytes.end());

    const std::expected<cc::RecipeId, cc::Diagnostic> result = table.InternComputed(std::move(sameKey), sameBytes);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, id);
    EXPECT_EQ(table.Count(), 1u);
}

// A real collision in 128 bits cannot be found, so the test forces one: InternComputed takes a key and bytes that
// the caller chose, and here the key of a stored recipe comes with bytes that differ in one byte.
TEST(RecipeTableTest, OtherBytesUnderTheSameKeyReturnKeyCollisionAndAddNothing)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::ParamsBox box = cc::ParamsBox::Make(cctest::ConstParams{});
    const cc::RecipeId id = InternOrFail(table, registry, "test.Const", cc::Quality::Final, box, {});

    const cc::Recipe& stored = table.Get(id);
    const std::span<const std::byte> storedBytes = table.CanonicalBytes(id);

    cc::Recipe forged{ .Key = stored.Key, .Kind = stored.Kind, .Level = stored.Level, .Params = box, .Inputs = {} };
    std::vector<std::byte> otherBytes(storedBytes.begin(), storedBytes.end());
    otherBytes.back() = static_cast<std::byte>(~static_cast<uint8_t>(otherBytes.back()));

    const std::expected<cc::RecipeId, cc::Diagnostic> result =
        table.InternComputed(std::move(forged), std::move(otherBytes));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().Code, cc::DiagnosticCode::KeyCollision);
    EXPECT_EQ(result.error().Level, cc::Severity::Error);
    EXPECT_EQ(table.Count(), 1u);
    EXPECT_EQ(table.Find(stored.Key), id);
}

TEST(RecipeTableTest, CanonicalBytesAreWhatTheKeyHashes)
{
    const cc::KindRegistry registry = cctest::MakeFrozenRegistry();
    cc::RecipeTable table;

    const cc::RecipeId id =
        InternOrFail(table, registry, "test.Const", cc::Quality::Preview, cc::ParamsBox::Make(cctest::ConstParams{}), {});

    foundation::content::HashSink sink;
    sink.WriteRawBytes(table.CanonicalBytes(id));
    const foundation::content::Hash128 hash = sink.Finish();

    EXPECT_EQ(table.Get(id).Key, (foundation::content::RecipeKey{ hash.Low, hash.High }));
}

namespace
{
    template<typename P>
    foundation::content::RecipeKey ReferenceKey(std::string_view kind, uint32_t version, cc::Quality quality,
                                                const P& params,
                                                std::span<const foundation::content::RecipeInput> inputs)
    {
        const cc::RecipeParams<P> wrapped{ quality, params };
        const std::expected<foundation::content::RecipeKey, foundation::content::KeyError> key =
            foundation::content::MakeRecipeKey(kind, version, wrapped, inputs);

        EXPECT_TRUE(key.has_value());

        if (!key.has_value())
        {
            return foundation::content::RecipeKey{};
        }

        return *key;
    }

    cc::RecipeId InternOrFail(cc::RecipeTable& table, const cc::KindRegistry& registry, std::string_view kind_name,
                              cc::Quality quality, const cc::ParamsBox& params,
                              std::span<const cc::RecipeInputRef> inputs)
    {
        const std::optional<cc::KindId> kind = registry.Find(kind_name);
        EXPECT_TRUE(kind.has_value());

        if (!kind.has_value())
        {
            return cc::RecipeId{};
        }

        const std::expected<cc::RecipeId, cc::Diagnostic> id = table.Intern(registry, *kind, quality, params, inputs);
        EXPECT_TRUE(id.has_value());

        if (!id.has_value())
        {
            return cc::RecipeId{};
        }

        return *id;
    }
}
