#include <gtest/gtest.h>

#include "content/Key.hpp"
#include "content/RecipeKey.hpp"
#include "reflect/Describe.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace keytest
{
    enum class WaveKind : uint8_t
    {
        Invalid = 0,
        Sine,
        Square,
    };

    struct NoiseParams
    {
        WaveKind Kind{ WaveKind::Invalid };
        float Amplitude{ 0.0f };
        uint32_t Seed{ 0 };
    };

    struct TileParams
    {
        std::vector<float> Weights;
        std::optional<int32_t> Limit;
        std::string Label;
    };

    struct PlanetParams
    {
        NoiseParams Noise;
        TileParams Tile;
        double Radius{ 0.0 };
        bool Wrap{ false };
    };

    // same Describe name, fields declared in the opposite order
    struct PairXY
    {
        float X{ 0.0f };
        float Y{ 0.0f };
    };

    struct PairYX
    {
        float Y{ 0.0f };
        float X{ 0.0f };
    };

    // the same enum before and after a case is inserted
    enum class LetterBefore : uint8_t
    {
        Invalid = 0,
        Alpha,
        Gamma,
    };

    enum class LetterAfter : uint8_t
    {
        Invalid = 0,
        Alpha,
        Beta,
        Gamma,
    };

    struct HolderBefore
    {
        LetterBefore Letter{ LetterBefore::Invalid };
    };

    struct HolderAfter
    {
        LetterAfter Letter{ LetterAfter::Invalid };
    };

    struct SignedZero
    {
        float Value{ 0.0f };
    };

    struct SameFieldsA
    {
        float Value{ 0.0f };
    };

    struct SameFieldsB
    {
        float Value{ 0.0f };
    };

    struct Wide32
    {
        int32_t Value{ 0 };
    };

    struct Wide64
    {
        int64_t Value{ 0 };
    };

    // seven bytes of padding after Flag and seven after Tail
    struct Padded
    {
        uint8_t Flag{ 0 };
        double Value{ 0.0 };
        uint8_t Tail{ 0 };
    };

    foundation::content::RecipeKey KeyOf(std::string_view kind, uint32_t kind_version, const auto& params);
    foundation::content::RecipeKey KeyOf(const auto& params);
    std::string Hex(const foundation::content::RecipeKey& key);
}

namespace foundation::reflect
{
    template<>
    struct Describe<keytest::WaveKind>
    {
        static constexpr std::string_view Name = "WaveKind";
        static constexpr std::array<EnumCase<keytest::WaveKind>, 3> Cases
        {
            EnumCase<keytest::WaveKind>{ "Invalid", keytest::WaveKind::Invalid },
            EnumCase<keytest::WaveKind>{ "Sine", keytest::WaveKind::Sine },
            EnumCase<keytest::WaveKind>{ "Square", keytest::WaveKind::Square },
        };
    };

    template<>
    struct Describe<keytest::NoiseParams>
    {
        using T = keytest::NoiseParams;

        static constexpr std::string_view Name = "NoiseParams";
        static constexpr std::tuple Fields
        {
            Field<T, keytest::WaveKind>{ "Kind", &T::Kind },
            Field<T, float>{ "Amplitude", &T::Amplitude },
            Field<T, uint32_t>{ "Seed", &T::Seed },
        };
    };

    template<>
    struct Describe<keytest::TileParams>
    {
        using T = keytest::TileParams;

        static constexpr std::string_view Name = "TileParams";
        static constexpr std::tuple Fields
        {
            Field<T, std::vector<float>>{ "Weights", &T::Weights },
            Field<T, std::optional<int32_t>>{ "Limit", &T::Limit },
            Field<T, std::string>{ "Label", &T::Label },
        };
    };

    template<>
    struct Describe<keytest::PlanetParams>
    {
        using T = keytest::PlanetParams;

        static constexpr std::string_view Name = "PlanetParams";
        static constexpr std::tuple Fields
        {
            Field<T, keytest::NoiseParams>{ "Noise", &T::Noise },
            Field<T, keytest::TileParams>{ "Tile", &T::Tile },
            Field<T, double>{ "Radius", &T::Radius },
            Field<T, bool>{ "Wrap", &T::Wrap },
        };
    };

    template<>
    struct Describe<keytest::PairXY>
    {
        using T = keytest::PairXY;

        static constexpr std::string_view Name = "Pair";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "X", &T::X },
            Field<T, float>{ "Y", &T::Y },
        };
    };

    template<>
    struct Describe<keytest::PairYX>
    {
        using T = keytest::PairYX;

        static constexpr std::string_view Name = "Pair";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Y", &T::Y },
            Field<T, float>{ "X", &T::X },
        };
    };

    template<>
    struct Describe<keytest::LetterBefore>
    {
        static constexpr std::string_view Name = "Letter";
        static constexpr std::array<EnumCase<keytest::LetterBefore>, 3> Cases
        {
            EnumCase<keytest::LetterBefore>{ "Invalid", keytest::LetterBefore::Invalid },
            EnumCase<keytest::LetterBefore>{ "Alpha", keytest::LetterBefore::Alpha },
            EnumCase<keytest::LetterBefore>{ "Gamma", keytest::LetterBefore::Gamma },
        };
    };

    template<>
    struct Describe<keytest::LetterAfter>
    {
        static constexpr std::string_view Name = "Letter";
        static constexpr std::array<EnumCase<keytest::LetterAfter>, 4> Cases
        {
            EnumCase<keytest::LetterAfter>{ "Invalid", keytest::LetterAfter::Invalid },
            EnumCase<keytest::LetterAfter>{ "Alpha", keytest::LetterAfter::Alpha },
            EnumCase<keytest::LetterAfter>{ "Beta", keytest::LetterAfter::Beta },
            EnumCase<keytest::LetterAfter>{ "Gamma", keytest::LetterAfter::Gamma },
        };
    };

    template<>
    struct Describe<keytest::HolderBefore>
    {
        using T = keytest::HolderBefore;

        static constexpr std::string_view Name = "Holder";
        static constexpr std::tuple Fields
        {
            Field<T, keytest::LetterBefore>{ "Letter", &T::Letter },
        };
    };

    template<>
    struct Describe<keytest::HolderAfter>
    {
        using T = keytest::HolderAfter;

        static constexpr std::string_view Name = "Holder";
        static constexpr std::tuple Fields
        {
            Field<T, keytest::LetterAfter>{ "Letter", &T::Letter },
        };
    };

    template<>
    struct Describe<keytest::SignedZero>
    {
        using T = keytest::SignedZero;

        static constexpr std::string_view Name = "SignedZero";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<keytest::SameFieldsA>
    {
        using T = keytest::SameFieldsA;

        static constexpr std::string_view Name = "SameFieldsA";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<keytest::SameFieldsB>
    {
        using T = keytest::SameFieldsB;

        static constexpr std::string_view Name = "SameFieldsB";
        static constexpr std::tuple Fields
        {
            Field<T, float>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<keytest::Wide32>
    {
        using T = keytest::Wide32;

        static constexpr std::string_view Name = "Wide";
        static constexpr std::tuple Fields
        {
            Field<T, int32_t>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<keytest::Wide64>
    {
        using T = keytest::Wide64;

        static constexpr std::string_view Name = "Wide";
        static constexpr std::tuple Fields
        {
            Field<T, int64_t>{ "Value", &T::Value },
        };
    };

    template<>
    struct Describe<keytest::Padded>
    {
        using T = keytest::Padded;

        static constexpr std::string_view Name = "Padded";
        static constexpr std::tuple Fields
        {
            Field<T, uint8_t>{ "Flag", &T::Flag },
            Field<T, double>{ "Value", &T::Value },
            Field<T, uint8_t>{ "Tail", &T::Tail },
        };
    };
}

using namespace foundation::content;

static_assert(!std::is_convertible_v<RecipeKey, BlobKey>);
static_assert(!std::is_convertible_v<BlobKey, RecipeKey>);
static_assert(!std::is_convertible_v<RecipeKey, ArtifactHash>);
static_assert(!std::is_convertible_v<ArtifactHash, BlobKey>);
static_assert(!std::is_constructible_v<RecipeKey, BlobKey>);
static_assert(!std::is_constructible_v<BlobKey, RecipeKey>);

TEST(ContentKeysTest, KnownAnswerNoiseParams)
{
    const keytest::NoiseParams params{ keytest::WaveKind::Square, 1.5f, 42 };

    // The expected text comes from known_answer_keys.py, an independent encoder (xxhash module), not from this code.
    EXPECT_EQ(keytest::Hex(keytest::KeyOf("noise", 1, params)), "7ba4e24170aa9638c3960df276b86f8c");
}

TEST(ContentKeysTest, KnownAnswerVectorAndOptional)
{
    const keytest::TileParams params{ { 0.25f, 2.0f }, 7, "grass" };

    EXPECT_EQ(keytest::Hex(keytest::KeyOf("tile", 3, params)), "707ed9479c755ec6b61963abc2e5829b");
}

TEST(ContentKeysTest, KnownAnswerNestedWithInputs)
{
    const keytest::PlanetParams params
    {
        keytest::NoiseParams{ keytest::WaveKind::Sine, -0.5f, 0xDEADBEEFu },
        keytest::TileParams{},
        6371.0,
        true,
    };

    const std::array<RecipeInput, 2> inputs
    {
        RecipeInput{ "heights", RecipeKey{ 1, 2 }, "Value" },
        RecipeInput{ "mask", BlobKey{ 3, 4 }, "" },
    };

    const std::expected<RecipeKey, KeyError> result = MakeRecipeKey("planet", 2, params, inputs);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(keytest::Hex(*result), "888ebf680a4890b877c43aaaa94b26d6");
}

TEST(ContentKeysTest, KnownAnswerInputsWithOutputs)
{
    const keytest::NoiseParams params{ keytest::WaveKind::Square, 1.5f, 42 };

    const std::array<RecipeInput, 2> inputs
    {
        RecipeInput{ "source", RecipeKey{ 5, 6 }, "Height" },
        RecipeInput{ "blob", BlobKey{ 7, 8 }, "" },
    };

    const std::expected<RecipeKey, KeyError> result = MakeRecipeKey("noise", 1, params, inputs);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(keytest::Hex(*result), "16bd189e4e267807cbeda75a37708191");
}

TEST(ContentKeysTest, SameValuesGiveSameKey)
{
    const keytest::NoiseParams first{ keytest::WaveKind::Sine, 2.0f, 9 };
    const keytest::NoiseParams second{ keytest::WaveKind::Sine, 2.0f, 9 };

    EXPECT_EQ(keytest::KeyOf(first), keytest::KeyOf(second));
}

TEST(ContentKeysTest, KindNameAndKindVersionChangeTheKey)
{
    const keytest::NoiseParams params{ keytest::WaveKind::Sine, 2.0f, 9 };

    const RecipeKey base = keytest::KeyOf("noise", 1, params);

    EXPECT_NE(base, keytest::KeyOf("noise2", 1, params));
    EXPECT_NE(base, keytest::KeyOf("noise", 2, params));
}

TEST(ContentKeysTest, SwappingTwoFloatFieldDeclarationsChangesTheKey)
{
    keytest::PairXY first;
    first.X = 1.0f;
    first.Y = 2.0f;

    keytest::PairYX second;
    second.X = 1.0f;
    second.Y = 2.0f;

    EXPECT_NE(keytest::KeyOf(first), keytest::KeyOf(second));
}

TEST(ContentKeysTest, SwappingFieldDeclarationsChangesTheKeyEvenWhenTheValuesAreEqual)
{
    // equal values, so only the field names and their order can tell the two apart
    keytest::PairXY first;
    first.X = 1.5f;
    first.Y = 1.5f;

    keytest::PairYX second;
    second.X = 1.5f;
    second.Y = 1.5f;

    EXPECT_NE(keytest::KeyOf(first), keytest::KeyOf(second));
}

TEST(ContentKeysTest, InsertingAnEnumCaseDoesNotChangeTheOtherCasesKey)
{
    const keytest::HolderBefore gamma_before{ keytest::LetterBefore::Gamma };
    const keytest::HolderAfter gamma_after{ keytest::LetterAfter::Gamma };
    const keytest::HolderBefore alpha_before{ keytest::LetterBefore::Alpha };
    const keytest::HolderAfter alpha_after{ keytest::LetterAfter::Alpha };
    const keytest::HolderAfter beta_after{ keytest::LetterAfter::Beta };

    // Gamma is 2 before the insert and 3 after it
    EXPECT_EQ(keytest::KeyOf(gamma_before), keytest::KeyOf(gamma_after));
    EXPECT_EQ(keytest::KeyOf(alpha_before), keytest::KeyOf(alpha_after));
    EXPECT_NE(keytest::KeyOf(gamma_after), keytest::KeyOf(beta_after));
}

TEST(ContentKeysTest, NegativeZeroAndPositiveZeroGiveDifferentKeys)
{
    const keytest::SignedZero positive{ 0.0f };
    const keytest::SignedZero negative{ -0.0f };

    ASSERT_TRUE(std::signbit(negative.Value));
    EXPECT_NE(keytest::KeyOf(positive), keytest::KeyOf(negative));
}

TEST(ContentKeysTest, DifferentDescribeNamesGiveDifferentKeys)
{
    const keytest::SameFieldsA first{ 3.0f };
    const keytest::SameFieldsB second{ 3.0f };

    EXPECT_NE(keytest::KeyOf(first), keytest::KeyOf(second));
}

TEST(ContentKeysTest, GarbageInPaddingDoesNotChangeTheKey)
{
    const keytest::Padded clean{ 1, 2.0, 3 };

    alignas(keytest::Padded) std::byte storage[sizeof(keytest::Padded)];
    std::memset(storage, 0xCD, sizeof(storage));
    const keytest::Padded* dirty = new (storage) keytest::Padded{ 1, 2.0, 3 };

    // the test setup is only valid when the padding really holds garbage
    const std::byte padding_byte = storage[offsetof(keytest::Padded, Value) - 1];
    ASSERT_EQ(padding_byte, std::byte{ 0xCD });

    EXPECT_EQ(keytest::KeyOf(clean), keytest::KeyOf(*dirty));
}

TEST(ContentKeysTest, NaNInANestedFieldReturnsItsPath)
{
    keytest::PlanetParams params;
    params.Noise.Amplitude = std::numeric_limits<float>::quiet_NaN();

    const std::expected<RecipeKey, KeyError> result = MakeRecipeKey("planet", 1, params, {});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().Code, KeyErrorCode::NaNParameter);
    EXPECT_EQ(result.error().FieldPath, "Noise.Amplitude");
}

TEST(ContentKeysTest, NaNInAVectorElementReturnsItsIndexedPath)
{
    keytest::PlanetParams params;
    params.Tile.Weights = { 1.0f, std::numeric_limits<float>::quiet_NaN() };

    const std::expected<RecipeKey, KeyError> result = MakeRecipeKey("planet", 1, params, {});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().FieldPath, "Tile.Weights[1]");
}

TEST(ContentKeysTest, NaNInADoubleFieldReturnsItsPath)
{
    keytest::PlanetParams params;
    params.Radius = std::numeric_limits<double>::quiet_NaN();

    const std::expected<RecipeKey, KeyError> result = MakeRecipeKey("planet", 1, params, {});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().FieldPath, "Radius");
}

TEST(ContentKeysTest, InfinityIsNotRefused)
{
    keytest::PlanetParams params;
    params.Radius = std::numeric_limits<double>::infinity();

    const std::expected<RecipeKey, KeyError> result = MakeRecipeKey("planet", 1, params, {});

    EXPECT_TRUE(result.has_value());
}

TEST(ContentKeysTest, RecipeKeyAndBlobKeyFromTheSameBytesDiffer)
{
    const std::array<std::byte, 4> bytes{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 } };

    const keytest::SameFieldsA params{ 1.0f };
    const RecipeKey recipe_key = keytest::KeyOf(params);
    const BlobKey blob_key = HashBlob(bytes);

    EXPECT_TRUE(recipe_key.Low != blob_key.Low || recipe_key.High != blob_key.High);
    EXPECT_EQ(blob_key, HashBlob(bytes));

    const std::array<std::byte, 4> other_bytes{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 5 } };
    EXPECT_NE(blob_key, HashBlob(other_bytes));
}

TEST(ContentKeysTest, HashBlobSeparatesLengthFromContent)
{
    const std::array<std::byte, 2> zeros{ std::byte{ 0 }, std::byte{ 0 } };

    EXPECT_NE(HashBlob(std::span<const std::byte>{ zeros }.first(1)), HashBlob(zeros));
    EXPECT_NE(HashBlob(std::span<const std::byte>{}), HashBlob(std::span<const std::byte>{ zeros }.first(1)));
}

TEST(ContentKeysTest, SignedAndWiderIntegersGiveDifferentKeys)
{
    const keytest::Wide32 narrow{ 1 };
    const keytest::Wide64 wide{ 1 };

    EXPECT_NE(keytest::KeyOf(narrow), keytest::KeyOf(wide));
}

TEST(ContentKeysTest, InputChangesChangeTheKey)
{
    const keytest::NoiseParams params{ keytest::WaveKind::Sine, 2.0f, 9 };

    const std::array<RecipeInput, 1> first_inputs{ RecipeInput{ "source", RecipeKey{ 1, 2 }, "Height" } };
    const std::array<RecipeInput, 1> other_key{ RecipeInput{ "source", RecipeKey{ 1, 3 }, "Height" } };
    const std::array<RecipeInput, 1> other_slot{ RecipeInput{ "other", RecipeKey{ 1, 2 }, "Height" } };
    const std::array<RecipeInput, 1> other_output{ RecipeInput{ "source", RecipeKey{ 1, 2 }, "Normal" } };
    const std::array<RecipeInput, 1> as_blob{ RecipeInput{ "source", BlobKey{ 1, 2 }, "" } };

    const RecipeKey base = *MakeRecipeKey("noise", 1, params, first_inputs);

    EXPECT_NE(base, *MakeRecipeKey("noise", 1, params, other_key));
    EXPECT_NE(base, *MakeRecipeKey("noise", 1, params, other_slot));
    EXPECT_NE(base, *MakeRecipeKey("noise", 1, params, other_output));
    EXPECT_NE(base, *MakeRecipeKey("noise", 1, params, as_blob));
    EXPECT_NE(base, *MakeRecipeKey("noise", 1, params, {}));
}

TEST(ContentKeysTest, HexIsHighWordThenLowWordInLowercase)
{
    const RecipeKey key{ 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL };

    EXPECT_EQ(ToHex(key), "fedcba98765432100123456789abcdef");

    std::array<char, 32> out{};
    WriteHex(key, out);
    EXPECT_EQ(std::string_view(out.data(), out.size()), "fedcba98765432100123456789abcdef");
}

TEST(ContentKeysTest, KeysOrderByLowThenHighAndWorkAsMapKeys)
{
    const RecipeKey smaller{ 1, 9 };
    const RecipeKey larger{ 2, 0 };

    EXPECT_LT(smaller, larger);

    std::unordered_map<RecipeKey, int32_t, KeyHasher<RecipeTag>> map;
    map[smaller] = 5;
    map[larger] = 6;

    EXPECT_EQ(map.at(RecipeKey{ 1, 9 }), 5);
    EXPECT_EQ(map.at(RecipeKey{ 2, 0 }), 6);
}

namespace keytest
{
    foundation::content::RecipeKey KeyOf(std::string_view kind, uint32_t kind_version, const auto& params)
    {
        const std::expected<foundation::content::RecipeKey, foundation::content::KeyError> result =
            foundation::content::MakeRecipeKey(kind, kind_version, params, {});

        EXPECT_TRUE(result.has_value());

        if (!result.has_value())
        {
            return foundation::content::RecipeKey{};
        }

        return *result;
    }

    foundation::content::RecipeKey KeyOf(const auto& params)
    {
        return KeyOf("test", 1, params);
    }

    std::string Hex(const foundation::content::RecipeKey& key)
    {
        return foundation::content::ToHex(key);
    }
}
