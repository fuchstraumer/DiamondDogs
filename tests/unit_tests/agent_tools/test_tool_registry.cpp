#include "ToolLine.hpp"
#include "ToolRegistry.hpp"
#include "ToolReply.hpp"
#include "ToolResult.hpp"
#include "content/JsonSink.hpp"
#include "nlohmann/json.hpp"
#include "reflect/Describe.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
    enum class Mood : uint8_t
    {
        Invalid = 0,
        Calm,
        Stormy,
    };

    struct Inner
    {
        float Height{ 1.0f };
        int32_t Count{ 2 };
    };

    struct Sea
    {
        float WindForce{ 5.0f };
        float Depth{ 50.0f };
        Mood Weather{ Mood::Calm };
        Inner Details{};
        std::array<float, 2> Pair{ 1.0f, 2.0f };
        float Locked{ 3.0f };
        float Secret{ 4.0f };
    };

    struct RecursiveNode
    {
        float Value{ 0.0f };
        std::vector<RecursiveNode> Children;
    };

    struct MoveParams
    {
        std::array<float, 3> Position{};
        float Speed{ 1.0f };
        float Scale{ 1.0f };
    };

    struct MoveReply
    {
        float Length{ 0.0f };
    };

    struct MoveCommand
    {
        static constexpr std::string_view Name{ "test.move" };
        static constexpr std::string_view Doc{ "Moves the thing." };
        using Params = MoveParams;
        using Reply = MoveReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        int32_t CallCount{ 0 };
    };

    struct LaterCommand
    {
        static constexpr std::string_view Name{ "test.later" };
        static constexpr std::string_view Doc{ "Keeps the token until the test answers." };
        using Params = agent::NoParams;
        using Reply = agent::NoReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);

        std::optional<agent::ReplyToken<Reply>> Held;
    };

    struct DropCommand
    {
        static constexpr std::string_view Name{ "test.drop" };
        static constexpr std::string_view Doc{ "Drops the token." };
        using Params = agent::NoParams;
        using Reply = agent::NoReply;

        void Run(const Params& params, agent::ReplyToken<Reply> token);
    };

    struct RecordingSink final : agent::ReplySink
    {
        void Finish(int64_t request_id, std::string result_json) final;
        void Fail(int64_t request_id, agent::ToolResult failure) final;

        int64_t LastRequest{ -1 };
        std::optional<std::string> Result;
        std::optional<agent::ToolResult> Failure;
    };

    struct ChangeLog
    {
        void Record(std::string_view path);

        std::vector<std::string> Paths;
    };

    [[nodiscard]] std::string ReadJson(const agent::ToolRegistry& registry, std::string_view path);
}

template<>
struct foundation::reflect::Describe<Mood>
{
    static constexpr std::string_view Name{ "test.Mood" };
    static constexpr std::array Cases
    {
        EnumCase<Mood>{ "Invalid", Mood::Invalid, {} },
        EnumCase<Mood>{ "Calm", Mood::Calm, "The water is flat." },
        EnumCase<Mood>{ "Stormy", Mood::Stormy, {} },
    };
};

template<>
struct foundation::reflect::Describe<Inner>
{
    static constexpr std::string_view Name{ "test.Inner" };
    static constexpr std::tuple Fields
    {
        Field<Inner, float>{ "Height", &Inner::Height },
        Field<Inner, int32_t>{ "Count", &Inner::Count },
    };
};

template<>
struct foundation::reflect::Describe<Sea>
{
    static constexpr std::string_view Name{ "test.Sea" };
    static constexpr std::string_view Doc{ "A small sea." };
    static constexpr std::tuple Fields
    {
        Field<Sea, float>{ "WindForce", &Sea::WindForce, { .Unit = "Bft", .Range = FieldRange{ 0.0, 12.0 },
            .Doc = "Wind." } },
        Field<Sea, float>{ "Depth", &Sea::Depth, { .Range = FieldRange{ 0.1, k_NoLimit } } },
        Field<Sea, Mood>{ "Weather", &Sea::Weather },
        Field<Sea, Inner>{ "Details", &Sea::Details },
        Field<Sea, std::array<float, 2>>{ "Pair", &Sea::Pair, { .Range = FieldRange{ 0.0, 10.0 } } },
        Field<Sea, float>{ "Locked", &Sea::Locked, { .ReadOnly = true } },
        Field<Sea, float>{ "Secret", &Sea::Secret, { .Hidden = true } },
    };
};

template<>
struct foundation::reflect::Describe<RecursiveNode>
{
    static constexpr std::string_view Name{ "test.RecursiveNode" };
    static constexpr std::tuple Fields
    {
        Field<RecursiveNode, float>{ "Value", &RecursiveNode::Value },
        Field<RecursiveNode, std::vector<RecursiveNode>>{ "Children", &RecursiveNode::Children },
    };
};

template<>
struct foundation::reflect::Describe<MoveParams>
{
    static constexpr std::string_view Name{ "test.MoveParams" };
    static constexpr std::tuple Fields
    {
        Field<MoveParams, std::array<float, 3>>{ "Position", &MoveParams::Position, { .Required = true } },
        Field<MoveParams, float>{ "Speed", &MoveParams::Speed, { .Range = FieldRange{ 0.0, 2.0 },
            .Required = true } },
        Field<MoveParams, float>{ "Scale", &MoveParams::Scale },
    };
};

template<>
struct foundation::reflect::Describe<MoveReply>
{
    static constexpr std::string_view Name{ "test.MoveReply" };
    static constexpr std::tuple Fields
    {
        Field<MoveReply, float>{ "Length", &MoveReply::Length },
    };
};

namespace
{
    const std::array<agent::NamedValue, 4> k_GoodMove
    {
        agent::NamedValue{ "Position[0]", foundation::reflect::ScalarValue{ 3.0 } },
        agent::NamedValue{ "Position[1]", foundation::reflect::ScalarValue{ 4.0 } },
        agent::NamedValue{ "Position[2]", foundation::reflect::ScalarValue{ 0.0 } },
        agent::NamedValue{ "Speed", foundation::reflect::ScalarValue{ 1.0 } },
    };

    class ToolRegistryTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            const agent::ObjectChanged changed = agent::ObjectChanged::Create<ChangeLog,
                &ChangeLog::Record>(&changes);

            ASSERT_EQ(registry.AddObject("world.sea", sea, changed), agent::ToolResultCode::Success);
            ASSERT_EQ(registry.AddCommand(move), agent::ToolResultCode::Success);
            ASSERT_EQ(registry.AddCommand(later), agent::ToolResultCode::Success);
            ASSERT_EQ(registry.AddCommand(drop), agent::ToolResultCode::Success);
            registry.Freeze();
        }

        Sea sea{};
        ChangeLog changes;
        MoveCommand move;
        LaterCommand later;
        DropCommand drop;
        agent::ToolRegistry registry;
        RecordingSink sink;
    };
}

TEST_F(ToolRegistryTest, GetReturnsAFieldAndAWholeObject)
{
    EXPECT_EQ(ReadJson(registry, "world.sea.WindForce"), "5");
    EXPECT_EQ(ReadJson(registry, "world.sea.Details.Count"), "2");
    EXPECT_EQ(ReadJson(registry, "world.sea.Pair[1]"), "2");
    EXPECT_EQ(ReadJson(registry, "world.sea.Weather"), "\"Calm\"");

    const nlohmann::json whole = nlohmann::json::parse(ReadJson(registry, "world.sea"));

    EXPECT_EQ(whole["Depth"], 50.0);
    EXPECT_EQ(whole["Details"]["Height"], 1.0);
}

TEST_F(ToolRegistryTest, SetWritesAndReportsTheFieldPathToTheCallback)
{
    ASSERT_TRUE(registry.SetValue("world.sea.Depth", foundation::reflect::ScalarValue{ 12.0 }).Succeeded());
    ASSERT_TRUE(registry.SetValue("world.sea.Details.Count",
                foundation::reflect::ScalarValue{ int64_t{ 9 } }).Succeeded());

    EXPECT_EQ(sea.Depth, 12.0f);
    EXPECT_EQ(sea.Details.Count, 9);
    EXPECT_EQ(changes.Paths, (std::vector<std::string>{ "Depth", "Details.Count" }));
}

TEST(ToolResultTest, AnUnsetRecordIsNotASuccess)
{
    EXPECT_FALSE(agent::ToolResult{}.Succeeded());
    EXPECT_TRUE(agent::ToolResult{ .Code = agent::ToolResultCode::Success }.Succeeded());
}

TEST_F(ToolRegistryTest, RefusedSetChangesNothingAndCallsNoCallback)
{
    const Sea before = sea;
    const agent::ToolResult written =
        registry.SetValue("world.sea.WindForce", foundation::reflect::ScalarValue{ 13.0 });

    ASSERT_FALSE(written.Succeeded());
    EXPECT_EQ(written.Code, agent::ToolResultCode::ValueRefused);
    EXPECT_EQ(written.Path, "world.sea.WindForce");
    EXPECT_NE(written.Message.find("(0 to 12)"), std::string::npos);
    EXPECT_EQ(sea.WindForce, before.WindForce);
    EXPECT_TRUE(changes.Paths.empty());
}

TEST_F(ToolRegistryTest, ReadOnlyFieldRefusesAWrite)
{
    const agent::ToolResult written =
        registry.SetValue("world.sea.Locked", foundation::reflect::ScalarValue{ 1.0 });

    ASSERT_FALSE(written.Succeeded());
    EXPECT_EQ(sea.Locked, 3.0f);
}

TEST_F(ToolRegistryTest, UnknownFieldSuggestsTheNearestNameAndAppliesNothing)
{
    const agent::ToolResult written =
        registry.SetValue("world.sea.windforse", foundation::reflect::ScalarValue{ 1.0 });

    ASSERT_FALSE(written.Succeeded());
    EXPECT_EQ(written.Offset, static_cast<int32_t>(std::string_view{ "world.sea." }.size()));
    EXPECT_EQ(written.Suggestions, (std::vector<std::string>{ "WindForce" }));
    EXPECT_EQ(sea.WindForce, 5.0f);
}

TEST_F(ToolRegistryTest, SuggestionsLookInsideTheOwnerAndSkipHiddenFields)
{
    const agent::ToolResult nested =
        registry.SetValue("world.sea.Details.Hieght", foundation::reflect::ScalarValue{ 1.0 });

    ASSERT_FALSE(nested.Succeeded());
    EXPECT_EQ(nested.Suggestions, (std::vector<std::string>{ "Height" }));

    const agent::ToolResult hidden =
        registry.SetValue("world.sea.Secrets", foundation::reflect::ScalarValue{ 1.0 });

    ASSERT_FALSE(hidden.Succeeded());
    EXPECT_TRUE(hidden.Suggestions.empty());
}

TEST_F(ToolRegistryTest, UnknownObjectSuggestsTheNearestObject)
{
    const agent::ToolResult written =
        registry.SetValue("world.see.WindForce", foundation::reflect::ScalarValue{ 1.0 });

    ASSERT_FALSE(written.Succeeded());
    EXPECT_EQ(written.Code, agent::ToolResultCode::UnknownObject);
    EXPECT_EQ(written.Suggestions, (std::vector<std::string>{ "world.sea" }));
}

TEST_F(ToolRegistryTest, HiddenFieldIsLeftOutOfSchemasAndAPathStillReachesIt)
{
    const nlohmann::json all = nlohmann::json::parse(registry.DescribeAll());
    const nlohmann::json& properties = all["objects"][0]["schema"]["properties"];

    EXPECT_TRUE(properties.contains("WindForce"));
    EXPECT_FALSE(properties.contains("Secret"));
    EXPECT_EQ(ReadJson(registry, "world.sea.Secret"), "4");
}

TEST_F(ToolRegistryTest, SchemaStatesMetadataAndOmitsAnOpenLimit)
{
    const nlohmann::json all = nlohmann::json::parse(registry.DescribeAll());
    const nlohmann::json& schema = all["objects"][0]["schema"];
    const nlohmann::json& properties = schema["properties"];

    EXPECT_EQ(schema["description"], "A small sea.");
    EXPECT_EQ(properties["WindForce"]["type"], "number");
    EXPECT_EQ(properties["WindForce"]["minimum"], 0.0);
    EXPECT_EQ(properties["WindForce"]["maximum"], 12.0);
    EXPECT_EQ(properties["WindForce"]["x-unit"], "Bft");
    EXPECT_EQ(properties["WindForce"]["description"], "Wind.");
    EXPECT_EQ(properties["Depth"]["minimum"], 0.1);
    EXPECT_FALSE(properties["Depth"].contains("maximum"));
    EXPECT_EQ(properties["Locked"]["readOnly"], true);
    EXPECT_EQ(properties["Weather"]["enum"], (std::vector<std::string>{ "Invalid", "Calm", "Stormy" }));
    EXPECT_EQ(properties["Weather"]["x-case-docs"]["Calm"], "The water is flat.");
    EXPECT_EQ(properties["Details"]["properties"]["Count"]["type"], "integer");
    EXPECT_EQ(properties["Pair"]["minItems"], 2);
    EXPECT_EQ(properties["Pair"]["maxItems"], 2);
    EXPECT_EQ(properties["Pair"]["items"]["maximum"], 10.0);
}

TEST_F(ToolRegistryTest, CommandSchemaListsRequiredFieldsAndTheReply)
{
    std::expected<std::string, agent::ToolResult> described = registry.Describe("test.move");

    ASSERT_TRUE(described.has_value());

    const nlohmann::json command = nlohmann::json::parse(*described);

    EXPECT_EQ(command["doc"], "Moves the thing.");
    EXPECT_EQ(command["params"]["required"], (std::vector<std::string>{ "Position", "Speed" }));
    EXPECT_TRUE(command["reply"]["properties"].contains("Length"));
}

TEST_F(ToolRegistryTest, DescribeReachesOneField)
{
    std::expected<std::string, agent::ToolResult> described = registry.Describe("world.sea.WindForce");

    ASSERT_TRUE(described.has_value());

    const nlohmann::json field = nlohmann::json::parse(*described);

    EXPECT_EQ(field["path"], "world.sea.WindForce");
    EXPECT_EQ(field["schema"]["maximum"], 12.0);
}

TEST_F(ToolRegistryTest, CallRunsTheCommandAndEncodesItsReply)
{
    registry.Call("test.move", k_GoodMove, agent::PendingReply{ sink, 7 });

    EXPECT_EQ(move.CallCount, 1);
    EXPECT_EQ(sink.LastRequest, 7);
    ASSERT_TRUE(sink.Result.has_value());
    EXPECT_EQ(*sink.Result, "{\"Length\":5}");
}

TEST_F(ToolRegistryTest, MissingRequiredArgumentNeverRunsTheCommand)
{
    registry.Call("test.move", std::span{ k_GoodMove }.first(3u), agent::PendingReply{ sink, 1 });

    EXPECT_EQ(move.CallCount, 0);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::MissingArgument);
    EXPECT_EQ(sink.Failure->Path, "Speed");
}

TEST_F(ToolRegistryTest, PartOfARequiredArrayIsMissing)
{
    const std::array<agent::NamedValue, 3> arguments
    {
        agent::NamedValue{ "Position[0]", foundation::reflect::ScalarValue{ 3.0 } },
        agent::NamedValue{ "Position[1]", foundation::reflect::ScalarValue{ 4.0 } },
        agent::NamedValue{ "Speed", foundation::reflect::ScalarValue{ 1.0 } },
    };

    registry.Call("test.move", arguments, agent::PendingReply{ sink, 1 });

    EXPECT_EQ(move.CallCount, 0);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::MissingArgument);
    EXPECT_EQ(sink.Failure->Path, "Position");
}

TEST_F(ToolRegistryTest, DuplicateArgumentIsRefused)
{
    const std::array<agent::NamedValue, 5> arguments
    {
        k_GoodMove[0], k_GoodMove[1], k_GoodMove[2], k_GoodMove[3], k_GoodMove[3],
    };

    registry.Call("test.move", arguments, agent::PendingReply{ sink, 1 });

    EXPECT_EQ(move.CallCount, 0);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::DuplicateArgument);
}

TEST_F(ToolRegistryTest, ArgumentOutsideItsLimitsNeverRunsTheCommand)
{
    std::array<agent::NamedValue, 4> arguments = k_GoodMove;
    arguments[3].Value = foundation::reflect::ScalarValue{ 3.0 };

    registry.Call("test.move", arguments, agent::PendingReply{ sink, 1 });

    EXPECT_EQ(move.CallCount, 0);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::ValueRefused);
    EXPECT_NE(sink.Failure->Message.find("(0 to 2)"), std::string::npos);
}

TEST_F(ToolRegistryTest, UnknownArgumentSuggestsAField)
{
    std::array<agent::NamedValue, 5> arguments{};
    std::ranges::copy(k_GoodMove, arguments.begin());
    arguments[4] = agent::NamedValue{ "Scael", foundation::reflect::ScalarValue{ 1.0 } };

    registry.Call("test.move", arguments, agent::PendingReply{ sink, 1 });

    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Suggestions, (std::vector<std::string>{ "Scale" }));
}

TEST_F(ToolRegistryTest, UnknownCommandSuggestsTheNearestCommand)
{
    registry.Call("test.mvoe", {}, agent::PendingReply{ sink, 1 });

    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::UnknownCommand);
    EXPECT_EQ(sink.Failure->Suggestions, (std::vector<std::string>{ "test.move" }));
}

TEST_F(ToolRegistryTest, CommandCanAnswerLater)
{
    registry.Call("test.later", {}, agent::PendingReply{ sink, 3 });

    EXPECT_FALSE(sink.Result.has_value());
    EXPECT_FALSE(sink.Failure.has_value());
    ASSERT_TRUE(later.Held.has_value());

    later.Held->Complete(agent::NoReply{});

    EXPECT_EQ(sink.LastRequest, 3);
    ASSERT_TRUE(sink.Result.has_value());
    EXPECT_EQ(*sink.Result, "{}");
}

TEST_F(ToolRegistryTest, DroppedTokenFailsWithNoReply)
{
    registry.Call("test.drop", {}, agent::PendingReply{ sink, 4 });

    EXPECT_EQ(sink.LastRequest, 4);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::NoReply);
}

TEST_F(ToolRegistryTest, AnswerAfterAMoveReachesTheSinkOnce)
{
    agent::PendingReply first{ sink, 5 };
    agent::PendingReply second = std::move(first);

    second.CompleteWithJson("1");

    EXPECT_EQ(sink.LastRequest, 5);
    EXPECT_TRUE(sink.Result.has_value());
    EXPECT_FALSE(sink.Failure.has_value());
}

TEST_F(ToolRegistryTest, LineFormReachesEveryVerb)
{
    agent::RunToolLine(registry, "set world.sea.Depth 12", sink, 1);
    ASSERT_TRUE(sink.Result.has_value());
    EXPECT_EQ(*sink.Result, "12");

    agent::RunToolLine(registry, "set world.sea.Weather Stormy", sink, 2);
    ASSERT_TRUE(sink.Result.has_value());
    EXPECT_EQ(*sink.Result, "\"Stormy\"");

    agent::RunToolLine(registry, "get world.sea.Details.Count", sink, 3);
    ASSERT_TRUE(sink.Result.has_value());
    EXPECT_EQ(*sink.Result, "2");

    agent::RunToolLine(registry, "call test.move Position[0]=3 Position[1]=4 Position[2]=0 Speed=1", sink, 4);
    ASSERT_TRUE(sink.Result.has_value());
    EXPECT_EQ(*sink.Result, "{\"Length\":5}");

    agent::RunToolLine(registry, "describe", sink, 5);
    ASSERT_TRUE(sink.Result.has_value());
    EXPECT_TRUE(nlohmann::json::parse(*sink.Result).contains("commands"));
}

TEST_F(ToolRegistryTest, LineFormReadsEachKindOfValue)
{
    agent::RunToolLine(registry, "set world.sea.Details.Count -4", sink, 1);
    ASSERT_TRUE(sink.Result.has_value());
    EXPECT_EQ(*sink.Result, "-4");

    agent::RunToolLine(registry, "set world.sea.Details.Height -1.5", sink, 2);
    ASSERT_TRUE(sink.Result.has_value());
    EXPECT_EQ(*sink.Result, "-1.5");

    agent::RunToolLine(registry, "set world.sea.Details.Count true", sink, 3);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::ValueRefused);
}

TEST_F(ToolRegistryTest, LineFormRefusesWhatIsNotAValue)
{
    agent::RunToolLine(registry, "set world.sea.Depth nan", sink, 1);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::ValueRefused);
    EXPECT_EQ(sea.Depth, 50.0f);

    agent::RunToolLine(registry, "set world.sea.Details.Count 3.5", sink, 2);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::ValueRefused);

    agent::RunToolLine(registry, "call test.move Speed", sink, 3);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::MalformedLine);

    agent::RunToolLine(registry, "frobnicate", sink, 4);
    ASSERT_TRUE(sink.Failure.has_value());
    EXPECT_EQ(sink.Failure->Code, agent::ToolResultCode::MalformedLine);
}

TEST(ToolRegistryAdd, RefusesDuplicatesOverlapsAndAnyAddAfterFreeze)
{
    Sea first{};
    Sea second{};
    MoveCommand command;
    MoveCommand sameName;
    agent::ToolRegistry registry;

    EXPECT_EQ(registry.AddObject("world.sea", first), agent::ToolResultCode::Success);
    EXPECT_EQ(registry.AddObject("world.sea", second), agent::ToolResultCode::DuplicateObject);
    EXPECT_EQ(registry.AddObject("world.sea.inner", second), agent::ToolResultCode::OverlappingObject);
    EXPECT_EQ(registry.AddObject("world", second), agent::ToolResultCode::OverlappingObject);
    EXPECT_EQ(registry.AddObject("world.seashell", second), agent::ToolResultCode::Success);
    EXPECT_EQ(registry.AddCommand(command), agent::ToolResultCode::Success);
    EXPECT_EQ(registry.AddCommand(sameName), agent::ToolResultCode::DuplicateCommand);

    registry.Freeze();

    Sea late{};

    EXPECT_EQ(registry.AddObject("late", late), agent::ToolResultCode::RegistryFrozen);
}

TEST(ToolRegistryAdd, ObjectsWithSimilarNamesResolveToTheRightOne)
{
    Sea sea{};
    Sea shell{};
    agent::ToolRegistry registry;

    sea.Depth = 1.0f;
    shell.Depth = 2.0f;
    ASSERT_EQ(registry.AddObject("world.sea", sea), agent::ToolResultCode::Success);
    ASSERT_EQ(registry.AddObject("world.seashell", shell), agent::ToolResultCode::Success);
    registry.Freeze();

    EXPECT_EQ(ReadJson(registry, "world.sea.Depth"), "1");
    EXPECT_EQ(ReadJson(registry, "world.seashell.Depth"), "2");
}

TEST(ToolRegistrySchema, RecursiveTypeEnds)
{
    RecursiveNode root{};
    agent::ToolRegistry registry;

    ASSERT_EQ(registry.AddObject("tree", root), agent::ToolResultCode::Success);
    registry.Freeze();

    const nlohmann::json all = nlohmann::json::parse(registry.DescribeAll());
    const nlohmann::json& items = all["objects"][0]["schema"]["properties"]["Children"]["items"];

    EXPECT_EQ(items["x-recursive"], true);
    EXPECT_FALSE(items.contains("properties"));
}

TEST(ToolResultJson, EncodesEveryMember)
{
    const agent::ToolResult failure
    {
        .Code = agent::ToolResultCode::UnknownCommand,
        .Message = "no \"such\" command",
        .Path = "a.b",
        .Offset = 2,
        .Suggestions = { "a.c" },
    };
    const nlohmann::json parsed = nlohmann::json::parse(agent::ResultToJson(failure));

    EXPECT_EQ(parsed["Code"], "UnknownCommand");
    EXPECT_EQ(parsed["Message"], "no \"such\" command");
    EXPECT_EQ(parsed["Path"], "a.b");
    EXPECT_EQ(parsed["Offset"], 2);
    EXPECT_EQ(parsed["Suggestions"][0], "a.c");
}

namespace
{
    void MoveCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
    {
        ++CallCount;

        const float x = params.Position[0];
        const float y = params.Position[1];
        const float z = params.Position[2];

        token.Complete(MoveReply{ .Length = std::sqrt(x * x + y * y + z * z) * params.Speed });
    }

    void LaterCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
    {
        static_cast<void>(params);
        Held.emplace(std::move(token));
    }

    void DropCommand::Run(const Params& params, agent::ReplyToken<Reply> token)
    {
        static_cast<void>(params);
        static_cast<void>(token);
    }

    void RecordingSink::Finish(int64_t request_id, std::string result_json)
    {
        LastRequest = request_id;
        Result = std::move(result_json);
        Failure.reset();
    }

    void RecordingSink::Fail(int64_t request_id, agent::ToolResult failure)
    {
        LastRequest = request_id;
        Failure = std::move(failure);
        Result.reset();
    }

    void ChangeLog::Record(std::string_view path)
    {
        Paths.emplace_back(path);
    }

    std::string ReadJson(const agent::ToolRegistry& registry, std::string_view path)
    {
        foundation::content::JsonSink json{ foundation::content::JsonStyle::Compact };
        const agent::ToolResult value = registry.GetValue(path, json);

        if (!value.Succeeded())
        {
            return "failed: " + value.Message;
        }

        foundation::content::JsonResult<std::string> document = json.Finish();

        return std::move(*document);
    }
}
