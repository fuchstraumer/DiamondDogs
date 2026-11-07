#include <gtest/gtest.h>

#include <xxhash.h>

#include "content/BytesSink.hpp"
#include "content/CanonicalSink.hpp"
#include "content/Encode.hpp"
#include "content/HashSink.hpp"
#include "content/Key.hpp"
#include "content/RecipeKey.hpp"
#include "reflect/Describe.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace bytestest
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

    struct VariantLeaf
    {
        int16_t Small{ 0 };
    };

    // one field of every kind that the sinks can write
    struct EverySink
    {
        bool Flag{ false };
        int8_t SignedByte{ 0 };
        uint64_t Wide{ 0 };
        float Single{ 0.0f };
        double Double{ 0.0 };
        std::string Text;
        WaveKind Wave{ WaveKind::Invalid };
        std::array<uint16_t, 3> Fixed{ 0, 0, 0 };
        std::optional<float> Maybe;
        std::variant<VariantLeaf, NoiseParams> Choice;
    };

    // Returns the XXH3-128 of the bytes, as the reference library computes it in one call.
    foundation::content::Hash128 HashOfBytes(std::span<const std::byte> bytes);
    foundation::content::Hash128 Finish(const foundation::content::HashSink& sink);
    std::string Hex(const foundation::content::Hash128& hash);

    template<typename Params>
    std::vector<std::byte> RecipeBytes(std::string_view kind, uint32_t version, const Params& params,
                                       std::span<const foundation::content::RecipeInput> inputs);
}

namespace foundation::reflect
{
    template<>
    struct Describe<bytestest::WaveKind>
    {
        static constexpr std::string_view Name = "WaveKind";
        static constexpr std::array<EnumCase<bytestest::WaveKind>, 3> Cases
        {
            EnumCase<bytestest::WaveKind>{ "Invalid", bytestest::WaveKind::Invalid },
            EnumCase<bytestest::WaveKind>{ "Sine", bytestest::WaveKind::Sine },
            EnumCase<bytestest::WaveKind>{ "Square", bytestest::WaveKind::Square },
        };
    };

    template<>
    struct Describe<bytestest::NoiseParams>
    {
        using T = bytestest::NoiseParams;

        static constexpr std::string_view Name = "NoiseParams";
        static constexpr std::tuple Fields
        {
            Field<T, bytestest::WaveKind>{ "Kind", &T::Kind },
            Field<T, float>{ "Amplitude", &T::Amplitude },
            Field<T, uint32_t>{ "Seed", &T::Seed },
        };
    };

    template<>
    struct Describe<bytestest::TileParams>
    {
        using T = bytestest::TileParams;

        static constexpr std::string_view Name = "TileParams";
        static constexpr std::tuple Fields
        {
            Field<T, std::vector<float>>{ "Weights", &T::Weights },
            Field<T, std::optional<int32_t>>{ "Limit", &T::Limit },
            Field<T, std::string>{ "Label", &T::Label },
        };
    };

    template<>
    struct Describe<bytestest::PlanetParams>
    {
        using T = bytestest::PlanetParams;

        static constexpr std::string_view Name = "PlanetParams";
        static constexpr std::tuple Fields
        {
            Field<T, bytestest::NoiseParams>{ "Noise", &T::Noise },
            Field<T, bytestest::TileParams>{ "Tile", &T::Tile },
            Field<T, double>{ "Radius", &T::Radius },
            Field<T, bool>{ "Wrap", &T::Wrap },
        };
    };

    template<>
    struct Describe<bytestest::VariantLeaf>
    {
        using T = bytestest::VariantLeaf;

        static constexpr std::string_view Name = "VariantLeaf";
        static constexpr std::tuple Fields
        {
            Field<T, int16_t>{ "Small", &T::Small },
        };
    };

    template<>
    struct Describe<bytestest::EverySink>
    {
        using T = bytestest::EverySink;

        static constexpr std::string_view Name = "EverySink";
        static constexpr std::tuple Fields
        {
            Field<T, bool>{ "Flag", &T::Flag },
            Field<T, int8_t>{ "SignedByte", &T::SignedByte },
            Field<T, uint64_t>{ "Wide", &T::Wide },
            Field<T, float>{ "Single", &T::Single },
            Field<T, double>{ "Double", &T::Double },
            Field<T, std::string>{ "Text", &T::Text },
            Field<T, bytestest::WaveKind>{ "Wave", &T::Wave },
            Field<T, std::array<uint16_t, 3>>{ "Fixed", &T::Fixed },
            Field<T, std::optional<float>>{ "Maybe", &T::Maybe },
            Field<T, std::variant<bytestest::VariantLeaf, bytestest::NoiseParams>>{ "Choice", &T::Choice },
        };
    };
}

using namespace foundation::content;

// The expected hex strings are the known answers of ContentKeysTest. They come from an independent Python encoder, so
// these tests show that BytesSink writes the stream that the reference hashes.
TEST(BytesSinkTest, BytesHashToTheKnownAnswerOfNoiseParams)
{
    const bytestest::NoiseParams params{ bytestest::WaveKind::Square, 1.5f, 42 };

    const std::vector<std::byte> bytes = bytestest::RecipeBytes("noise", 1, params, {});

    EXPECT_EQ(bytestest::Hex(bytestest::HashOfBytes(bytes)), "7ba4e24170aa9638c3960df276b86f8c");
}

TEST(BytesSinkTest, BytesHashToTheKnownAnswerOfVectorAndOptional)
{
    const bytestest::TileParams params{ { 0.25f, 2.0f }, 7, "grass" };

    const std::vector<std::byte> bytes = bytestest::RecipeBytes("tile", 3, params, {});

    EXPECT_EQ(bytestest::Hex(bytestest::HashOfBytes(bytes)), "707ed9479c755ec6b61963abc2e5829b");
}

TEST(BytesSinkTest, BytesHashToTheKnownAnswerOfNestedParamsWithInputs)
{
    const bytestest::PlanetParams params
    {
        bytestest::NoiseParams{ bytestest::WaveKind::Sine, -0.5f, 0xDEADBEEFu },
        bytestest::TileParams{},
        6371.0,
        true,
    };

    const std::array<RecipeInput, 2> inputs
    {
        RecipeInput{ "heights", RecipeKey{ 1, 2 }, "Value" },
        RecipeInput{ "mask", BlobKey{ 3, 4 }, "" },
    };

    const std::vector<std::byte> bytes = bytestest::RecipeBytes("planet", 2, params, inputs);

    EXPECT_EQ(bytestest::Hex(bytestest::HashOfBytes(bytes)), "888ebf680a4890b877c43aaaa94b26d6");
}

TEST(BytesSinkTest, BytesHashToTheSameValueAsHashSinkForEveryWriteKind)
{
    bytestest::EverySink value;
    value.Flag = true;
    value.SignedByte = -5;
    value.Wide = 0xFEDCBA9876543210ULL;
    value.Single = -0.0f;
    value.Double = 1.0e300;
    value.Text = "some text";
    value.Wave = bytestest::WaveKind::Square;
    value.Fixed = { 1, 2, 3 };
    value.Maybe = 4.5f;
    value.Choice = bytestest::NoiseParams{ bytestest::WaveKind::Sine, 2.0f, 9 };

    BytesSink bytes_sink;
    Encode(value, bytes_sink);
    bytes_sink.WriteKeyBytes(11, 22);

    HashSink hash_sink;
    Encode(value, hash_sink);
    hash_sink.WriteKeyBytes(11, 22);

    EXPECT_EQ(bytestest::Hex(bytestest::HashOfBytes(bytes_sink.Bytes())), bytestest::Hex(bytestest::Finish(hash_sink)));
}

TEST(BytesSinkTest, AbsentOptionalAndOtherVariantAlternativeStillMatchHashSink)
{
    bytestest::EverySink value;
    value.Choice = bytestest::VariantLeaf{ -3 };

    BytesSink bytes_sink;
    Encode(value, bytes_sink);

    HashSink hash_sink;
    Encode(value, hash_sink);

    EXPECT_EQ(bytestest::Hex(bytestest::HashOfBytes(bytes_sink.Bytes())), bytestest::Hex(bytestest::Finish(hash_sink)));
}

TEST(BytesSinkTest, RawBytesMatchHashSinkRawBytes)
{
    const std::array<std::byte, 5> blob{ std::byte{ 9 }, std::byte{ 8 }, std::byte{ 7 }, std::byte{ 6 }, std::byte{ 5 } };

    BytesSink bytes_sink;
    bytes_sink.WriteString("dd.blob");
    bytes_sink.WriteRawBytes(blob);

    HashSink hash_sink;
    hash_sink.WriteString("dd.blob");
    hash_sink.WriteRawBytes(blob);

    EXPECT_EQ(bytestest::Hex(bytestest::HashOfBytes(bytes_sink.Bytes())), bytestest::Hex(bytestest::Finish(hash_sink)));
}

TEST(BytesSinkTest, TakeBytesMovesTheBytesOutAndLeavesTheSinkEmpty)
{
    BytesSink sink;
    sink.WriteBool(true);

    ASSERT_FALSE(sink.Bytes().empty());

    const std::vector<std::byte> taken = sink.TakeBytes();

    // tag Bool, then the value
    ASSERT_EQ(taken.size(), 2u);
    EXPECT_EQ(taken[0], static_cast<std::byte>(CanonicalTag::Bool));
    EXPECT_EQ(taken[1], std::byte{ 1 });
    EXPECT_TRUE(sink.Bytes().empty());
}

TEST(BytesSinkTest, IntegerWritesTheWidthSignednessAndLowBytesOnly)
{
    BytesSink sink;
    sink.WriteInt(-2, 2, true);

    const std::vector<std::byte> taken = sink.TakeBytes();

    const std::vector<std::byte> expected
    {
        static_cast<std::byte>(CanonicalTag::Int), std::byte{ 2 }, std::byte{ 1 }, std::byte{ 0xFE }, std::byte{ 0xFF },
    };

    EXPECT_EQ(taken, expected);
}

namespace bytestest
{
    foundation::content::Hash128 HashOfBytes(std::span<const std::byte> bytes)
    {
        const XXH128_hash_t digest = XXH3_128bits(bytes.data(), bytes.size());

        return foundation::content::Hash128{ .Low = digest.low64, .High = digest.high64 };
    }

    foundation::content::Hash128 Finish(const foundation::content::HashSink& sink)
    {
        return sink.Finish();
    }

    std::string Hex(const foundation::content::Hash128& hash)
    {
        return foundation::content::ToHex(foundation::content::RecipeKey{ hash.Low, hash.High });
    }

    template<typename Params>
    std::vector<std::byte> RecipeBytes(std::string_view kind, uint32_t version, const Params& params,
                                       std::span<const foundation::content::RecipeInput> inputs)
    {
        foundation::content::BytesSink sink;
        foundation::content::detail::BeginRecipeKey(sink, kind, version);
        foundation::content::Encode(params, sink);
        foundation::content::detail::AppendRecipeInputs(sink, inputs);

        return sink.TakeBytes();
    }
}
