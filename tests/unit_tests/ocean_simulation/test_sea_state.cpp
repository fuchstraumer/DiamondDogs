#include <gtest/gtest.h>

#include "SceneCommands.hpp"
#include "SeaState.hpp"
#include "SeaState.reflect.hpp"
#include "content/BytesSink.hpp"
#include "content/Encode.hpp"
#include "content/EncodeErased.hpp"
#include "reflect/TypeInfo.hpp"
#include "reflect/ValueAccess.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using foundation::reflect::FieldInfo;
    using foundation::reflect::GetTypeInfo;
    using foundation::reflect::PathResult;
    using foundation::reflect::ScalarValue;
    using foundation::reflect::SetValue;
    using foundation::reflect::TypeInfo;
    using foundation::reflect::ValueResult;
    using ocean::SeaState;

    using MemberPair = std::pair<std::string_view, float SeaState::*>;

    // a list that is independent of the descriptor, so that a name paired with the wrong member fails here
    const std::array<MemberPair, 15> k_ExpectedMembers
    {
        MemberPair{ "WindForce", &SeaState::WindForce },
        MemberPair{ "Openness", &SeaState::Openness },
        MemberPair{ "WindDirection", &SeaState::WindDirection },
        MemberPair{ "Spread", &SeaState::Spread },
        MemberPair{ "Regularity", &SeaState::Regularity },
        MemberPair{ "RippleDetail", &SeaState::RippleDetail },
        MemberPair{ "Depth", &SeaState::Depth },
        MemberPair{ "SwellHeight", &SeaState::SwellHeight },
        MemberPair{ "SwellPeriod", &SeaState::SwellPeriod },
        MemberPair{ "SwellDirection", &SeaState::SwellDirection },
        MemberPair{ "SwellOrder", &SeaState::SwellOrder },
        MemberPair{ "HeightScale", &SeaState::HeightScale },
        MemberPair{ "WhitecapThreshold", &SeaState::WhitecapThreshold },
        MemberPair{ "FoamAmount", &SeaState::FoamAmount },
        MemberPair{ "CrestSharpness", &SeaState::CrestSharpness },
    };

    const TypeInfo& SeaInfo()
    {
        return GetTypeInfo<SeaState>();
    }

    std::vector<std::byte> EncodedBytes(const SeaState& state)
    {
        foundation::content::BytesSink sink;
        foundation::content::Encode(state, sink);

        return sink.TakeBytes();
    }

    void ExpectEveryFieldInsideItsLimits(const SeaState& state, std::string_view label)
    {
        for (const FieldInfo& field : SeaInfo().Fields)
        {
            const float value = *static_cast<const float*>(foundation::reflect::FieldAddress(field, &state));

            SeaState copy = state;
            const PathResult written =
                SetValue(SeaInfo(), &copy, field.Name, ScalarValue{ static_cast<double>(value) });

            EXPECT_TRUE(written.Succeeded()) << label << " " << field.Name << " = " << value;
        }
    }
}

TEST(SeaStateDescriptorTest, FieldsPairEachNameWithItsMemberInDeclarationOrder)
{
    const std::span<const FieldInfo> fields = SeaInfo().Fields;

    ASSERT_EQ(fields.size(), k_ExpectedMembers.size());

    const SeaState state{};

    for (size_t index = 0; index < fields.size(); ++index)
    {
        const auto& [name, member] = k_ExpectedMembers[index];

        EXPECT_EQ(fields[index].Name, name);
        EXPECT_EQ(foundation::reflect::FieldAddress(fields[index], &state), &(state.*member)) << name;
    }
}

TEST(SeaStateDescriptorTest, EveryFieldHasADoc)
{
    EXPECT_FALSE(SeaInfo().Doc.empty());

    for (const FieldInfo& field : SeaInfo().Fields)
    {
        EXPECT_FALSE(field.Metadata.Doc.empty()) << field.Name;
    }
}

TEST(SeaStateDescriptorTest, DefaultsAndPresetsAreInsideTheLimits)
{
    ExpectEveryFieldInsideItsLimits(SeaState{}, "default");

    for (const ocean::SeaStatePreset& preset : ocean::SeaStatePresets())
    {
        ExpectEveryFieldInsideItsLimits(preset.State, preset.Name);
    }
}

TEST(SeaStateDescriptorTest, TableWalkMatchesEncode)
{
    SeaState state{};

    foundation::content::BytesSink erasedSink;
    foundation::content::EncodeErased(SeaInfo(), &state, erasedSink);

    EXPECT_EQ(EncodedBytes(state), erasedSink.TakeBytes());

    for (const ocean::SeaStatePreset& preset : ocean::SeaStatePresets())
    {
        foundation::content::BytesSink presetSink;
        foundation::content::EncodeErased(SeaInfo(), &preset.State, presetSink);

        EXPECT_EQ(EncodedBytes(preset.State), presetSink.TakeBytes()) << preset.Name;
    }
}

TEST(SeaStateDescriptorTest, EachWriteReachesOnlyItsMember)
{
    for (const auto& [name, member] : k_ExpectedMembers)
    {
        const SeaState before{};
        SeaState after = before;

        // every limit in the descriptor contains 0.75, except the minimum of SwellPeriod
        const double value = name == "SwellPeriod" ? 3.25 : 0.75;

        ASSERT_TRUE(SetValue(SeaInfo(), &after, name, ScalarValue{ value }).Succeeded()) << name;
        EXPECT_EQ(after.*member, static_cast<float>(value)) << name;

        for (const auto& [otherName, otherMember] : k_ExpectedMembers)
        {
            if (otherName != name)
            {
                EXPECT_EQ(after.*otherMember, before.*otherMember) << name << " changed " << otherName;
            }
        }
    }
}

TEST(SeaStateDescriptorTest, WritesOutsideTheLimitsLeaveTheStateUnchanged)
{
    SeaState state{};

    const std::vector<std::byte> before = EncodedBytes(state);

    const std::array<std::pair<std::string_view, double>, 8> refusals
    {
        std::pair<std::string_view, double>{ "WindForce", 12.5 },
        std::pair<std::string_view, double>{ "WindForce", -1.0 },
        std::pair<std::string_view, double>{ "Openness", 1.5 },
        std::pair<std::string_view, double>{ "Regularity", 3.5 },
        std::pair<std::string_view, double>{ "Depth", 0.0 },
        std::pair<std::string_view, double>{ "SwellHeight", -0.5 },
        std::pair<std::string_view, double>{ "SwellPeriod", 1.0 },
        std::pair<std::string_view, double>{ "FoamAmount", -0.1 },
    };

    for (const auto& [name, value] : refusals)
    {
        const PathResult written = SetValue(SeaInfo(), &state, name, ScalarValue{ value });

        ASSERT_FALSE(written.Succeeded()) << name;
        EXPECT_EQ(written.Code, ValueResult::OutOfRange) << name;
    }

    EXPECT_EQ(EncodedBytes(state), before);
}

TEST(SeaCommandTest, AcceptsAFieldNameInAnyCaseAndStoresTheDeclaredName)
{
    const std::expected<SceneCommand, std::string> command = SceneCommand::Parse("sea windFORCE 7.5");

    ASSERT_TRUE(command.has_value()) << command.error();
    EXPECT_EQ(command->Type, SceneCommand::Kind::Sea);
    EXPECT_EQ(command->Text, "WindForce");
    EXPECT_EQ(command->Values[0], 7.5f);
}

TEST(SeaCommandTest, FindsEveryFieldByItsDeclaredNameAndAppliesItsLimit)
{
    for (const auto& [name, member] : k_ExpectedMembers)
    {
        const std::string line = std::string{ "sea " } + std::string{ name } + " 3.25";
        const bool insideLimits = SceneCommand::Parse(line).has_value();
        const bool limitIsBelowValue = name == "Openness" || name == "Spread" || name == "RippleDetail" ||
                                       name == "SwellOrder" || name == "WhitecapThreshold" ||
                                       name == "CrestSharpness" || name == "Regularity";

        EXPECT_EQ(insideLimits, !limitIsBelowValue) << line;
    }
}

TEST(SeaCommandTest, DirectionsHaveNoLimit)
{
    EXPECT_TRUE(SceneCommand::Parse("sea WindDirection 720").has_value());
    EXPECT_TRUE(SceneCommand::Parse("sea SwellDirection -720").has_value());
}

TEST(SeaCommandTest, ReportsTheLimitsOfARefusedValue)
{
    const std::expected<SceneCommand, std::string> command = SceneCommand::Parse("sea Openness 1.5");

    ASSERT_FALSE(command.has_value());
    EXPECT_NE(command.error().find("Openness"), std::string::npos);
    EXPECT_NE(command.error().find("(0 to 1)"), std::string::npos);
}

TEST(SeaCommandTest, RefusesNonFiniteValuesOnItsScriptLine)
{
    EXPECT_FALSE(SceneCommand::Parse("sea WindDirection nan").has_value());
    EXPECT_FALSE(SceneCommand::Parse("sea WindDirection inf").has_value());
}

TEST(SeaCommandTest, RefusesUnknownFieldsAndWords)
{
    EXPECT_FALSE(SceneCommand::Parse("sea FoamSpread 1.3").has_value());
    EXPECT_FALSE(SceneCommand::Parse("sea WindForce windy").has_value());
    EXPECT_FALSE(SceneCommand::Parse("sea WindForce").has_value());
}

TEST(SeaCommandTest, FindSeaStateFieldIgnoresCase)
{
    const FieldInfo* field = FindSeaStateField("swellperiod");

    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->Name, "SwellPeriod");
    EXPECT_EQ(FindSeaStateField("Nope"), nullptr);
}

TEST(SceneCommandParse, ToolKeepsTheRequestText)
{
    const std::expected<SceneCommand, std::string> command = SceneCommand::Parse("tool set ocean.sea.Depth 12");

    ASSERT_TRUE(command.has_value());
    EXPECT_EQ(command->Type, SceneCommand::Kind::Tool);
    EXPECT_EQ(command->Text, "set ocean.sea.Depth 12 ");
}

TEST(SceneCommandParse, ToolNeedsARequest)
{
    EXPECT_FALSE(SceneCommand::Parse("tool").has_value());
}

TEST(SceneCommandsLoadScript, MissingFileIsRefusedAndQueuesNothing)
{
    SceneCommands commands;

    const ScriptResult loaded = commands.LoadScript("this_script_does_not_exist.txt");

    EXPECT_EQ(loaded, ScriptResult::CannotOpen);
    EXPECT_FALSE(commands.Busy());
}

TEST(SceneCommandsLoadScript, ReadableFileQueuesItsLines)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "diamond_dogs_load_script_test.txt";

    {
        std::ofstream file(path);
        file << "pause on\n";
    }

    SceneCommands commands;

    const ScriptResult loaded = commands.LoadScript(path);

    std::filesystem::remove(path);

    EXPECT_EQ(loaded, ScriptResult::Success);
    EXPECT_TRUE(commands.Busy());
}
