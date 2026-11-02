#include <gtest/gtest.h>

#include "content/Encode.hpp"
#include "content/TextSink.hpp"
#include "reflect/Describe.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace texttest
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
    };

    std::string TextOf(const auto& value);
}

namespace foundation::reflect
{
    template<>
    struct Describe<texttest::WaveKind>
    {
        static constexpr std::string_view Name = "WaveKind";
        static constexpr std::array<EnumCase<texttest::WaveKind>, 3> Cases
        {
            EnumCase<texttest::WaveKind>{ "Invalid", texttest::WaveKind::Invalid },
            EnumCase<texttest::WaveKind>{ "Sine", texttest::WaveKind::Sine },
            EnumCase<texttest::WaveKind>{ "Square", texttest::WaveKind::Square },
        };
    };

    template<>
    struct Describe<texttest::NoiseParams>
    {
        using T = texttest::NoiseParams;

        static constexpr std::string_view Name = "NoiseParams";
        static constexpr std::tuple Fields
        {
            Field<T, texttest::WaveKind>{ "Kind", &T::Kind },
            Field<T, float>{ "Amplitude", &T::Amplitude },
            Field<T, uint32_t>{ "Seed", &T::Seed },
        };
    };

    template<>
    struct Describe<texttest::TileParams>
    {
        using T = texttest::TileParams;

        static constexpr std::string_view Name = "TileParams";
        static constexpr std::tuple Fields
        {
            Field<T, std::vector<float>>{ "Weights", &T::Weights },
            Field<T, std::optional<int32_t>>{ "Limit", &T::Limit },
            Field<T, std::string>{ "Label", &T::Label },
        };
    };

    template<>
    struct Describe<texttest::PlanetParams>
    {
        using T = texttest::PlanetParams;

        static constexpr std::string_view Name = "PlanetParams";
        static constexpr std::tuple Fields
        {
            Field<T, texttest::NoiseParams>{ "Noise", &T::Noise },
            Field<T, texttest::TileParams>{ "Tile", &T::Tile },
            Field<T, double>{ "Radius", &T::Radius },
        };
    };
}

TEST(ContentTextSinkTest, FlatStructPrintsOneFieldPerLine)
{
    const texttest::NoiseParams params{ texttest::WaveKind::Square, 1.5f, 42 };

    const std::string expected =
        "NoiseParams {\n"
        "  Kind: enum WaveKind::Square\n"
        "  Amplitude: f32 1.5 (0x3fc00000)\n"
        "  Seed: u32 42\n"
        "}\n";

    EXPECT_EQ(texttest::TextOf(params), expected);
}

TEST(ContentTextSinkTest, NestedStructWithVectorAndOptionalPrintsIndented)
{
    texttest::PlanetParams params;
    params.Noise = texttest::NoiseParams{ texttest::WaveKind::Sine, 0.25f, 7 };
    params.Tile = texttest::TileParams{ { 0.25f, 2.0f }, 7, "grass" };
    params.Radius = 6371.0;

    const std::string expected =
        "PlanetParams {\n"
        "  Noise: NoiseParams {\n"
        "    Kind: enum WaveKind::Sine\n"
        "    Amplitude: f32 0.25 (0x3e800000)\n"
        "    Seed: u32 7\n"
        "  }\n"
        "  Tile: TileParams {\n"
        "    Weights: array(2) [\n"
        "      [0]: f32 0.25 (0x3e800000)\n"
        "      [1]: f32 2 (0x40000000)\n"
        "    ]\n"
        "    Limit: some i32 7\n"
        "    Label: string \"grass\"\n"
        "  }\n"
        "  Radius: f64 6371 (0x40b8e30000000000)\n"
        "}\n";

    EXPECT_EQ(texttest::TextOf(params), expected);
}

TEST(ContentTextSinkTest, AbsentOptionalAndEmptyArrayPrint)
{
    const texttest::TileParams params{};

    const std::string expected =
        "TileParams {\n"
        "  Weights: array(0) [\n"
        "  ]\n"
        "  Limit: none\n"
        "  Label: string \"\"\n"
        "}\n";

    EXPECT_EQ(texttest::TextOf(params), expected);
}

namespace texttest
{
    std::string TextOf(const auto& value)
    {
        foundation::content::TextSink sink;
        foundation::content::Encode(value, sink);

        return sink.Text();
    }
}
